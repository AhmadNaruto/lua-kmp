# NoveLA Native Runtime Specification for lua-kmp

## 1. Overview & Motivation

This specification defines the transformation of `lua-kmp` into a high-performance, self-contained native scraping runtime for **NoveLA** (and the `novelx-sources` extension ecosystem), replacing Luaj and eliminating JVM/JNI bridging overhead for DOM traversal, regular expressions, JSON parsing, and cryptographic operations.

### Key Goals:
1. **Direct C-to-C DOM Manipulation**: Embed **Lexbor** (HTML5 parser + CSS selector engine) directly in the native C runtime, exposing an OOP interface matching NoveLA's Jsoup conventions (`doc:select()`, `el:text()`, etc.) directly to Lua scripts.
2. **Native Regular Expressions**: Embed **PCRE2** in C, giving Lua full regular expression capabilities (lookahead, lookbehind, non-greedy, Unicode) without JNI hops to Java `Pattern`.
3. **Zero-Overhead JSON Processing**: Embed **yyjson** in C for direct serialization/deserialization between JSON strings and Lua tables.
4. **Native Cryptography & Encoding**:
   - **Base64**: Native encoder/decoder without UTF-8 byte mangling.
   - **AES**: Portable AES-128/192/256 decryption (CBC/ECB/CTR with PKCS7 padding) via `tiny-aes-c`.
   - **Hashing**: MD5 and SHA-256 for URL token signing and anti-scraping verification.
5. **Bytecode Caching**: Support dumping and loading precompiled binary chunks (`.lbc`) to retain cold-start optimizations.
6. **Thread Safety & Multiplatform Portability**: Retain pure C99/C11 cross-compilation capability across Android (NDK/JNI), iOS (klib via cinterop), and Desktop JVM.

---

## 2. Architecture & Component Layout

```
native/
├── lua/                # Lua 5.5.1 core interpreter
├── lexbor/             # Lexbor HTML5 parser & CSS selector engine
├── pcre2/              # PCRE2 regex engine
├── yyjson/             # High-performance JSON parser/serializer (yyjson.c, yyjson.h)
├── crypto/             # Crypto & encoding utilities
│   ├── aes/            # tiny-aes-c (aes.c, aes.h)
│   ├── base64/         # base64.c, base64.h
│   └── hash/           # md5.c, sha256.c
├── modules/            # Lua C modules binding the native components
│   ├── lua_html.c      # Lexbor Lua binding (OOP DOM, Document, Element, Elements)
│   ├── lua_pcre.c      # PCRE2 Lua binding (regex_match, regex_replace)
│   ├── lua_json.c      # yyjson Lua binding (json_parse, json_stringify)
│   └── lua_crypto.c    # Crypto Lua binding (aes_decrypt, base64_*, md5, sha256)
├── shim/               # lua-kmp core shims (lua_shim.c, lua_all.c)
└── jni/                # JNI entry points (lua_jni.c)
```

---

## 3. Lua API Specification for NoveLA Extensions

The native modules inject the following standard functions and objects into the Lua environment:

### 3.1 HTML & DOM (Lexbor Binding)

#### Global Functions:
- `html_parse(html_string)`: Parses HTML and returns a `Document` userdata object.

#### Document & Element Methods (OOP Shosetsu-Style):
- `doc:select(css_query)` / `el:select(css_query)`: Returns an `Elements` list.
- `doc:selectFirst(css_query)` / `el:selectFirst(css_query)`: Returns the first matching `Element`, or `nil`.
- `el:text()`: Returns the combined text content of the element and children.
- `el:ownText()`: Returns only the direct text of this element.
- `el:html()`: Returns the inner HTML.
- `el:outerHtml()`: Returns the outer HTML.
- `el:attr(name)`: Returns the attribute value, or empty string `""` if absent.
- `el:hasAttr(name)`: Returns boolean `true`/`false`.
- `el:hasClass(name)`: Returns boolean `true`/`false`.
- `el:tagName()`: Returns the tag name (e.g. `"div"`, `"a"`).
- `el:parent()`: Returns the parent `Element`, or `nil`.
- `el:children()`: Returns an `Elements` list of child elements.
- `el:nextElementSibling()`: Returns the next sibling `Element`, or `nil`.
- `el:previousElementSibling()`: Returns previous sibling `Element`, or `nil`.
- `el:remove()`: Removes the element from the DOM tree.

#### Elements List Methods:
- `list:size()` / `list:length()` / `#list`: Total number of elements.
- `list:isEmpty()`: Boolean `true` if empty.
- `list:first()`: First `Element` or `nil`.
- `list:last()`: Last `Element` or `nil`.
- `list:get(i)`: `Element` at index `i` (1-indexed).
- `list:attr(name)`: Attribute value of the first element.
- `list:text()`: Concatenated text of all elements.
- `list:select(css_query)`: Sub-query over all elements in the list.
- `list:forEach(fn)`: Iterates `fn(el, index)`.

---

### 3.2 Regular Expressions (PCRE2 Binding)

- `regex_match(pattern, text)`:
  Matches `pattern` against `text`. Returns captures as multiple returns, or `nil` if no match.
- `regex_replace(pattern, replacement, text)`:
  Replaces occurrences of `pattern` in `text` with `replacement`.

---

### 3.3 JSON (yyjson Binding)

- `json_parse(json_string)`:
  Parses JSON and returns the corresponding Lua structure (tables, strings, numbers, booleans, nil).
- `json_stringify(lua_table, [pretty_bool])`:
  Serializes a Lua table to a JSON string.

---

### 3.4 Cryptography & Encoding

- `base64_encode(binary_or_string)`:
  Returns standard Base64 string.
- `base64_decode(base64_string)`:
  Returns decoded binary string (8-bit clean).
- `aes_decrypt(ciphertext_base64_or_raw, key, iv, [mode = "cbc", padding = "pkcs7"])`:
  Decrypts AES ciphertext using raw binary or string key and IV without UTF-8 corruption.
- `md5(string)`:
  Returns lowercase hex MD5 hash (32 characters).
- `sha256(string)`:
  Returns lowercase hex SHA-256 hash (64 characters).

---

## 4. Host (Kotlin/Android) Responsibilities

The host environment retains responsibility only for platform-specific capabilities:
1. **Network**:
   - `http_get(url, config)` and `http_post(url, config)` routed through NoveLA's `NetworkClient` (OkHttp with Cloudflare bypass, CookieJar, User-Agent management).
2. **Preferences & Storage**:
   - `get_preference(key, default)`, `set_preference(key, value)`.
   - `get_cookies(url)`, `set_cookies(url, cookies)`.
3. **Threading**:
   - Each `LuaState` instance is run inside a dedicated Single-Thread Executor (`Executors.newSingleThreadExecutor().asCoroutineDispatcher()`) to respect `lua-kmp` thread confinement.

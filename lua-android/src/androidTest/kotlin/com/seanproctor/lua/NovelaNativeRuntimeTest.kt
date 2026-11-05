package com.seanproctor.lua

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

class NovelaNativeRuntimeTest {

    @Test
    fun htmlParsingAndDomTraversal() {
        LuaState().use { lua ->
            val script = """
                local html = [[
                    <div id="content" class="main-body">
                        <h1 class="title">Book Title</h1>
                        <ul class="chapters">
                            <li><a href="/chap/1" class="ch-link">Chapter 1</a></li>
                            <li><a href="/chap/2" class="ch-link">Chapter 2</a></li>
                        </ul>
                    </div>
                ]]
                local doc = html_parse(html)
                local title = doc:selectFirst(".title"):text()
                local links = doc:select(".ch-link")
                local count = links:size()
                local first_href = links:first():attr("href")
                local second_text = links:get(2):text()
                return title, count, first_href, second_text
            """.trimIndent()

            val results = lua.eval(script)
            assertEquals(4, results.size)
            assertEquals(LuaValue.Str("Book Title"), results[0])
            assertEquals(LuaValue.Integer(2), results[1])
            assertEquals(LuaValue.Str("/chap/1"), results[2])
            assertEquals(LuaValue.Str("Chapter 2"), results[3])
        }
    }

    @Test
    fun regexMatchAndReplace() {
        LuaState().use { lua ->
            val script = """
                local text = "Chapter 123: The Beginning (2024-05-10)"
                local matches = regex_match(text, "(\\d+)")
                local replaced = regex_replace(text, "\\d{4}-\\d{2}-\\d{2}", "REDACTED")
                return matches[1], matches[2], replaced
            """.trimIndent()

            val results = lua.eval(script)
            assertEquals(3, results.size)
            assertEquals(LuaValue.Str("123"), results[0])
            assertEquals(LuaValue.Str("2024"), results[1])
            assertEquals(LuaValue.Str("Chapter 123: The Beginning (REDACTED)"), results[2])
        }
    }

    @Test
    fun jsonParsingAndStringifying() {
        LuaState().use { lua ->
            val script = """
                local json_str = '{"name":"NoveLA","version":2,"active":true,"tags":["manga","novel"]}'
                local data = json_parse(json_str)
                local name = data.name
                local tag1 = data.tags[1]
                local encoded = json_stringify(data)
                return name, tag1, encoded
            """.trimIndent()

            val results = lua.eval(script)
            assertEquals(3, results.size)
            assertEquals(LuaValue.Str("NoveLA"), results[0])
            assertEquals(LuaValue.Str("manga"), results[1])
            assertIs<LuaValue.Str>(results[2])
            assertTrue((results[2] as LuaValue.Str).value.contains("NoveLA"))
        }
    }

    @Test
    fun cryptoAndEncoding() {
        LuaState().use { lua ->
            val script = """
                local raw = "Hello NoveLA Native Runtime!"
                local b64 = base64_encode(raw)
                local decoded = base64_decode(b64)
                local m = md5("test")
                local s = sha256("test")
                return b64, decoded, m, s
            """.trimIndent()

            val results = lua.eval(script)
            assertEquals(4, results.size)
            assertEquals(LuaValue.Str("Hello NoveLA Native Runtime!"), results[1])
            // MD5 of "test" is 098f6bcd4621d373cade4e832627b4f6
            assertEquals(LuaValue.Str("098f6bcd4621d373cade4e832627b4f6"), results[2])
            // SHA256 of "test" is 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
            assertEquals(LuaValue.Str("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"), results[3])
        }
    }

    @Test
    fun bytecodeDumpAndLoadRoundTrip() {
        LuaState().use { lua ->
            val chunk = lua.load("local a, b = ... ; return a * 10 + b")
            val bytecode = lua.dump(chunk)
            assertTrue(bytecode.isNotEmpty())

            // Load dumped bytecode back into a new state
            LuaState().use { newLua ->
                val reloaded = newLua.load(bytecode)
                val res = newLua.call(reloaded, listOf(LuaValue.Integer(5), LuaValue.Integer(7)))
                assertEquals(listOf<LuaValue>(LuaValue.Integer(57)), res)
            }
        }
    }

    @Test
    fun globalHtmlFunctionsAndPropertyAccess() {
        LuaState().use { lua ->
            val script = """
                local html = [[
                    <div class="col-truyen-main">
                        <div class="row">
                            <div class="col-xs-7">
                                <h3><a href="/book/1.html" class="title-link">  Novel Title  </a></h3>
                            </div>
                            <span class="ad-banner">Advertisement</span>
                        </div>
                    </div>
                ]]
                -- 1. html_select on string
                local rows = html_select(html, ".col-truyen-main .row")
                local row = rows:first()

                -- 2. html_select_first on row.html
                local titleEl = html_select_first(row.html, "h3 > a")

                -- 3. row.text as property AND row:text() as method
                local textProp = string_clean(titleEl.text)
                local textMethod = string_clean(titleEl:text())

                -- 4. Attribute access (.href and :attr())
                local hrefProp = titleEl.href
                local hrefMethod = titleEl:attr("href")

                -- 5. html_attr global
                local globalAttr = html_attr(html, "a.title-link", "class")

                -- 6. html_remove
                local cleaned = html_remove(html, ".ad-banner")
                local hasAd = string.find(cleaned, "Advertisement") ~= nil

                return textProp, textMethod, hrefProp, hrefMethod, globalAttr, hasAd
            """.trimIndent()

            val results = lua.eval(script)
            assertEquals(6, results.size)
            assertEquals(LuaValue.Str("Novel Title"), results[0])
            assertEquals(LuaValue.Str("Novel Title"), results[1])
            assertEquals(LuaValue.Str("/book/1.html"), results[2])
            assertEquals(LuaValue.Str("/book/1.html"), results[3])
            assertEquals(LuaValue.Str("title-link"), results[4])
            assertEquals(LuaValue.Boolean(false), results[5])
        }
    }

    @Test
    fun stringAndUrlUtilities() {
        LuaState().use { lua ->
            val script = """
                local clean = string_clean(" \t Hello \n  world   from \r\n NoveLA  ")
                local trim = string_trim("   trimmed string   ")
                local starts = string_starts_with("https://example.com", "https://")
                local ends = string_ends_with("archive.tar.gz", ".gz")
                local parts = string_split("one,two,three", ",")
                local unesc = unescape_unicode("Title:\\u0020\\u004EoveLA")
                local encoded = url_encode("query test&value=123")
                local resolvedAbs = url_resolve("https://example.com/dir/page.html", "/root.html")
                local resolvedRel = url_resolve("https://example.com/dir/page.html", "sub.html")
                local resolvedProto = url_resolve("https://example.com", "//cdn.example.com/img.jpg")

                return clean, trim, starts, ends, parts[2], unesc, encoded, resolvedAbs, resolvedRel, resolvedProto
            """.trimIndent()

            val results = lua.eval(script)
            assertEquals(10, results.size)
            assertEquals(LuaValue.Str("Hello world from NoveLA"), results[0])
            assertEquals(LuaValue.Str("trimmed string"), results[1])
            assertEquals(LuaValue.Boolean(true), results[2])
            assertEquals(LuaValue.Boolean(true), results[3])
            assertEquals(LuaValue.Str("two"), results[4])
            assertEquals(LuaValue.Str("Title: NoveLA"), results[5])
            assertEquals(LuaValue.Str("query%20test%26value%3D123"), results[6])
            assertEquals(LuaValue.Str("https://example.com/root.html"), results[7])
            assertEquals(LuaValue.Str("https://example.com/dir/sub.html"), results[8])
            assertEquals(LuaValue.Str("https://cdn.example.com/img.jpg"), results[9])
        }
    }

    @Test
    fun bit32CompatibilityAndTableCreation() {
        LuaState().use { lua ->
            val script = """
                local a = bit32.band(3, 5)
                local b = bit32.bor(3, 5)
                local c = bit32.bxor(3, 5)
                local d = bit32.lshift(2, 3)
                return a, b, c, d
            """.trimIndent()

            val results = lua.eval(script)
            assertEquals(listOf(
                LuaValue.Integer(1),
                LuaValue.Integer(7),
                LuaValue.Integer(6),
                LuaValue.Integer(16)
            ), results)

            // Test newTable creation from Kotlin
            val table = lua.newTable()
            table[LuaValue.Str("status")] = LuaValue.Integer(200)
            table[LuaValue.Str("success")] = LuaValue.Bool(true)
            lua.setGlobal("response", table)

            val fromLua = lua.eval("return response.status, response.success")
            assertEquals(listOf(LuaValue.Integer(200), LuaValue.Bool(true)), fromLua)

            // Test Userdata marshal safety
            val docUserdata = lua.eval("return html_parse('<div>hello</div>')")
            assertEquals(1, docUserdata.size)
            assertIs<LuaValue.Userdata>(docUserdata[0])
            assertEquals("userdata", (docUserdata[0] as LuaValue.Userdata).typeName)
        }
    }
}


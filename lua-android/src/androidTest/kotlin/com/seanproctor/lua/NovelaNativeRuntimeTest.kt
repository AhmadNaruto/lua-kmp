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
}

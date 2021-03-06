package com.bugsnag.android

import com.bugsnag.android.BugsnagTestUtils.streamableToJson
import com.bugsnag.android.BugsnagTestUtils.streamableToJsonArray
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import java.io.File
import java.io.FileOutputStream

class ErrorReaderNumberTest {

    private var error: Error? = null

    @Before
    fun setUp() {
        val classLoader = ErrorReaderNumberTest::class.java.classLoader
        val input = classLoader!!.getResourceAsStream("stackframe_numbers.json")
        val fixtureFile = File.createTempFile("error", ".json")
        val output = FileOutputStream(fixtureFile)

        output.use {
            val buffer = ByteArray(1024)
            var read = input.read(buffer)

            while (read != -1) {
                it.write(buffer, 0, read)
                read = input.read(buffer)
            }
            it.flush()
        }
        error = ErrorReader.readError(Configuration("key"), fixtureFile)
    }

    @Test
    fun readErrorExceptionStacktrace() {
        val event = streamableToJsonArray(error!!.exceptions).getJSONObject(0)
        val stacktrace = event.getJSONArray("stacktrace")
        assertEquals(3, stacktrace.length().toLong())

        val frame0 = stacktrace.getJSONObject(0)
        assertEquals(2241790.1, frame0.getDouble("lineNumber"), 0.01)

        val frame1 = stacktrace.getJSONObject(1)
        assertEquals(150000000000, frame1.getLong("lineNumber"))

        val frame2 = stacktrace.getJSONObject(2)
        assertEquals(761, frame2.getInt("lineNumber").toLong())
    }

    @Test
    fun readErrorThreadStacktrace() {
        val thread = streamableToJson(error).getJSONArray("threads").getJSONObject(0)
        assertEquals(11236722452451234, thread.getLong("id"))

        val trace = thread.getJSONArray("stacktrace")
        assertEquals(160923409125093, trace.getJSONObject(0).getLong("lineNumber"))
        assertEquals(1566.5, trace.getJSONObject(1).getDouble("lineNumber"), 0.1)
    }
}

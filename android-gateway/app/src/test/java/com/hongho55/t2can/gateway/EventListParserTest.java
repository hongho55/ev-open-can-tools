package com.hongho55.t2can.gateway;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class EventListParserTest {
    @Test
    public void parsesBoundedReadOnlyIncidentMetadata() throws Exception {
        String body = "{\"schema\":\"t2can-incident-list-v1\","
                + "\"boardId\":\"board-test\",\"incidents\":["
                + "{\"id\":\"1-0\",\"size\":3,"
                + "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
                + "\"acknowledged\":false}]}";

        EventListParser.ParsedList parsed = EventListParser.parse(body);

        assertEquals("board-test", parsed.boardId);
        assertEquals(1, parsed.events.size());
        assertEquals("1-0", parsed.events.get(0).eventId);
        assertEquals(3L, parsed.events.get(0).size);
    }

    @Test(expected = IllegalArgumentException.class)
    public void rejectsBoardIdHeaderInjection() throws Exception {
        EventListParser.parse("{\"schema\":\"t2can-incident-list-v1\","
                + "\"boardId\":\"board\\r\\nX-Evil: yes\",\"incidents\":[]}");
    }

    @Test
    public void preservesMissingIntegrityHashForSafeSkip() throws Exception {
        EventListParser.ParsedList parsed = EventListParser.parse(
                "{\"schema\":\"t2can-incident-list-v1\","
                        + "\"boardId\":\"board-test\",\"incidents\":["
                        + "{\"id\":\"1-0\",\"size\":3,\"sha256\":null,"
                        + "\"acknowledged\":false}]}");

        assertEquals(null, parsed.events.get(0).sha256);
    }
}

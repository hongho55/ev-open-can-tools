package com.hongho55.t2can.gateway;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

final class EventListParser {
    private static final Pattern EVENT_ID = Pattern.compile("^[1-9][0-9]{0,18}-[0-9]{1,3}$");
    private static final Pattern SHA256 = Pattern.compile("^[0-9a-fA-F]{64}$");
    private static final long MAX_EVENT_BYTES = 8L * 1024L * 1024L;

    private EventListParser() {
    }

    static ParsedList parse(String body) throws Exception {
        if (body == null || body.length() > 128 * 1024) {
            throw new IllegalArgumentException("event list too large");
        }
        JSONObject root = new JSONObject(body);
        if (!"t2can-incident-list-v1".equals(root.getString("schema"))) {
            throw new IllegalArgumentException("unsupported event list schema");
        }
        String boardId = root.getString("boardId");
        if (boardId.isEmpty() || boardId.length() > 64 || boardId.indexOf('/') >= 0
                || hasControlCharacter(boardId)) {
            throw new IllegalArgumentException("invalid board id");
        }
        JSONArray jsonEvents = root.getJSONArray("incidents");
        if (jsonEvents.length() > 256) {
            throw new IllegalArgumentException("too many incidents");
        }
        List<EventRecord> events = new ArrayList<>();
        for (int i = 0; i < jsonEvents.length(); i++) {
            JSONObject item = jsonEvents.getJSONObject(i);
            String eventId = item.getString("id");
            if (!EVENT_ID.matcher(eventId).matches()) {
                throw new IllegalArgumentException("invalid event id");
            }
            long size = item.getLong("size");
            if (size < 0 || size > MAX_EVENT_BYTES) {
                throw new IllegalArgumentException("invalid event size");
            }
            String sha256 = item.isNull("sha256") ? null : item.getString("sha256").toLowerCase();
            if (sha256 != null && !SHA256.matcher(sha256).matches()) {
                throw new IllegalArgumentException("invalid event hash");
            }
            events.add(new EventRecord(eventId, size, sha256,
                    item.getBoolean("acknowledged")));
        }
        return new ParsedList(boardId, events);
    }

    private static boolean hasControlCharacter(String value) {
        for (int i = 0; i < value.length(); i++) {
            if (value.charAt(i) < 0x20) {
                return true;
            }
        }
        return false;
    }

    static final class ParsedList {
        final String boardId;
        final List<EventRecord> events;

        ParsedList(String boardId, List<EventRecord> events) {
            this.boardId = boardId;
            this.events = events;
        }
    }
}

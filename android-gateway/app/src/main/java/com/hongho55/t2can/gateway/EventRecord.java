package com.hongho55.t2can.gateway;

final class EventRecord {
    final String eventId;
    final long size;
    final String sha256;
    final boolean acknowledged;

    EventRecord(String eventId, long size, String sha256, boolean acknowledged) {
        this.eventId = eventId;
        this.size = size;
        this.sha256 = sha256;
        this.acknowledged = acknowledged;
    }
}

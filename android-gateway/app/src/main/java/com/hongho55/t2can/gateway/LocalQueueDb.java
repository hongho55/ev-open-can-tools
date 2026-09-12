package com.hongho55.t2can.gateway;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;

import java.util.ArrayList;
import java.util.List;

final class LocalQueueDb extends SQLiteOpenHelper {
    private static final String DATABASE_NAME = "gateway_queue.db";
    private static final int DATABASE_VERSION = 1;
    private static final String TABLE = "incidents";

    LocalQueueDb(Context context) {
        super(context, DATABASE_NAME, null, DATABASE_VERSION);
    }

    @Override
    public void onCreate(SQLiteDatabase db) {
        db.execSQL("CREATE TABLE incidents ("
                + "board_id TEXT NOT NULL, event_id TEXT NOT NULL, size INTEGER NOT NULL,"
                + "sha256 TEXT NOT NULL, local_path TEXT NOT NULL, state TEXT NOT NULL,"
                + "attempts INTEGER NOT NULL DEFAULT 0, lease_owner TEXT, lease_until INTEGER,"
                + "next_attempt_at INTEGER NOT NULL, last_error TEXT, remote_status TEXT,"
                + "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,"
                + "PRIMARY KEY(board_id, event_id))");
        db.execSQL("CREATE INDEX incidents_upload_idx ON incidents(state, next_attempt_at, created_at)");
    }

    @Override
    public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
        throw new IllegalStateException("unsupported queue schema upgrade");
    }

    synchronized void enqueue(String boardId, EventRecord event, String localPath, long now) {
        SQLiteDatabase db = getWritableDatabase();
        Item existing = get(db, boardId, event.eventId);
        if (existing != null) {
            if (existing.size != event.size || !existing.sha256.equals(event.sha256)) {
                throw new IllegalStateException("queue content conflict");
            }
            return;
        }
        ContentValues values = new ContentValues();
        values.put("board_id", boardId);
        values.put("event_id", event.eventId);
        values.put("size", event.size);
        values.put("sha256", event.sha256);
        values.put("local_path", localPath);
        values.put("state", "QUEUED_LOCAL");
        values.put("attempts", 0);
        values.put("next_attempt_at", now);
        values.put("created_at", now);
        values.put("updated_at", now);
        db.insertOrThrow(TABLE, null, values);
    }

    synchronized Item claimUpload(String owner, long now, long leaseMillis) {
        SQLiteDatabase db = getWritableDatabase();
        db.beginTransaction();
        try {
            ContentValues recover = new ContentValues();
            recover.put("state", "QUEUED_LOCAL");
            recover.putNull("lease_owner");
            recover.putNull("lease_until");
            recover.put("updated_at", now);
            db.update(TABLE, recover, "state = ? AND lease_until <= ?",
                    new String[]{"UPLOADING", Long.toString(now)});

            Item item = null;
            try (Cursor cursor = db.query(TABLE, null,
                    "state = ? AND next_attempt_at <= ?",
                    new String[]{"QUEUED_LOCAL", Long.toString(now)}, null, null,
                    "created_at ASC", "1")) {
                if (cursor.moveToFirst()) {
                    item = from(cursor);
                }
            }
            if (item == null) {
                db.setTransactionSuccessful();
                return null;
            }
            ContentValues claim = new ContentValues();
            claim.put("state", "UPLOADING");
            claim.put("attempts", item.attempts + 1);
            claim.put("lease_owner", owner);
            claim.put("lease_until", now + leaseMillis);
            claim.put("updated_at", now);
            int claimed = db.update(TABLE, claim, "board_id = ? AND event_id = ? AND state = ?",
                    new String[]{item.boardId, item.eventId, "QUEUED_LOCAL"});
            if (claimed != 1) {
                throw new IllegalStateException("upload claim was lost");
            }
            db.setTransactionSuccessful();
            return get(db, item.boardId, item.eventId);
        } finally {
            db.endTransaction();
        }
    }

    synchronized void markUploadFailure(Item item, String errorCode, long now) {
        requireLease(item);
        long delay = Math.min(15L * 60L * 1000L, 1L << Math.min(item.attempts, 8));
        ContentValues values = new ContentValues();
        values.put("state", "QUEUED_LOCAL");
        values.putNull("lease_owner");
        values.putNull("lease_until");
        values.put("next_attempt_at", now + delay);
        values.put("last_error", errorCode);
        values.put("updated_at", now);
        int updated = getWritableDatabase().update(TABLE, values,
                "board_id = ? AND event_id = ? AND state = ? AND lease_owner = ?",
                new String[]{item.boardId, item.eventId, "UPLOADING", item.leaseOwner});
        if (updated != 1) {
            throw new IllegalStateException("upload failure transition was lost");
        }
    }

    synchronized void markMacCommitted(Item item, String remoteStatus, long now) {
        requireLease(item);
        if (!"stored".equals(remoteStatus) && !"already_stored".equals(remoteStatus)) {
            throw new IllegalArgumentException("remote commit not durable");
        }
        ContentValues values = new ContentValues();
        values.put("state", "ACK_PENDING");
        values.putNull("lease_owner");
        values.putNull("lease_until");
        values.put("next_attempt_at", now);
        values.putNull("last_error");
        values.put("remote_status", remoteStatus);
        values.put("updated_at", now);
        int updated = getWritableDatabase().update(TABLE, values,
                "board_id = ? AND event_id = ? AND state = ? AND lease_owner = ?",
                new String[]{item.boardId, item.eventId, "UPLOADING", item.leaseOwner});
        if (updated != 1) {
            throw new IllegalStateException("Mac commit transition was lost");
        }
    }

    synchronized void markAcked(Item item, long now) {
        if (!"ACK_PENDING".equals(item.state)) {
            throw new IllegalStateException("ESP32 ACK before Mac commit");
        }
        ContentValues values = new ContentValues();
        values.put("state", "ACKED");
        values.put("updated_at", now);
        int updated = getWritableDatabase().update(TABLE, values,
                "board_id = ? AND event_id = ? AND state = ?",
                new String[]{item.boardId, item.eventId, "ACK_PENDING"});
        if (updated != 1) {
            throw new IllegalStateException("ACK transition was lost");
        }
    }

    synchronized List<Item> pendingAcks() {
        List<Item> result = new ArrayList<>();
        try (Cursor cursor = getReadableDatabase().query(TABLE, null,
                "state = ?", new String[]{"ACK_PENDING"}, null, null, "created_at ASC")) {
            while (cursor.moveToNext()) {
                result.add(from(cursor));
            }
        }
        return result;
    }

    synchronized Item find(String boardId, String eventId) {
        return get(getReadableDatabase(), boardId, eventId);
    }

    private static void requireLease(Item item) {
        if (!"UPLOADING".equals(item.state) || item.leaseOwner == null) {
            throw new IllegalStateException("upload lease is not active");
        }
    }

    private Item get(SQLiteDatabase db, String boardId, String eventId) {
        try (Cursor cursor = db.query(TABLE, null, "board_id = ? AND event_id = ?",
                new String[]{boardId, eventId}, null, null, null, "1")) {
            return cursor.moveToFirst() ? from(cursor) : null;
        }
    }

    private static Item from(Cursor cursor) {
        return new Item(
                cursor.getString(cursor.getColumnIndexOrThrow("board_id")),
                cursor.getString(cursor.getColumnIndexOrThrow("event_id")),
                cursor.getLong(cursor.getColumnIndexOrThrow("size")),
                cursor.getString(cursor.getColumnIndexOrThrow("sha256")),
                cursor.getString(cursor.getColumnIndexOrThrow("local_path")),
                cursor.getString(cursor.getColumnIndexOrThrow("state")),
                cursor.getInt(cursor.getColumnIndexOrThrow("attempts")),
                cursor.getString(cursor.getColumnIndexOrThrow("lease_owner")),
                cursor.isNull(cursor.getColumnIndexOrThrow("lease_until"))
                        ? null : cursor.getLong(cursor.getColumnIndexOrThrow("lease_until")),
                cursor.getLong(cursor.getColumnIndexOrThrow("next_attempt_at")),
                cursor.getString(cursor.getColumnIndexOrThrow("remote_status")));
    }

    static final class Item {
        final String boardId;
        final String eventId;
        final long size;
        final String sha256;
        final String localPath;
        final String state;
        final int attempts;
        final String leaseOwner;
        final Long leaseUntil;
        final long nextAttemptAt;
        final String remoteStatus;

        Item(String boardId, String eventId, long size, String sha256, String localPath,
             String state, int attempts, String leaseOwner, Long leaseUntil,
             long nextAttemptAt, String remoteStatus) {
            this.boardId = boardId;
            this.eventId = eventId;
            this.size = size;
            this.sha256 = sha256;
            this.localPath = localPath;
            this.state = state;
            this.attempts = attempts;
            this.leaseOwner = leaseOwner;
            this.leaseUntil = leaseUntil;
            this.nextAttemptAt = nextAttemptAt;
            this.remoteStatus = remoteStatus;
        }
    }
}

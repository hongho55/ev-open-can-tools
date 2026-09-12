package com.hongho55.t2can.gateway;

import android.content.Context;
import android.content.SharedPreferences;

final class GatewayStatus {
    private static final String PREFS = "gateway_status";
    private static final String LAST_RESULT = "last_result";
    private static final String LAST_TIME = "last_time";

    private GatewayStatus() {
    }

    static void write(Context context, String result) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putString(LAST_RESULT, result)
                .putLong(LAST_TIME, System.currentTimeMillis())
                .apply();
    }

    static String read(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String result = prefs.getString(LAST_RESULT, "not_run");
        long time = prefs.getLong(LAST_TIME, 0L);
        return time == 0L ? result : result + " (" + time + ")";
    }
}

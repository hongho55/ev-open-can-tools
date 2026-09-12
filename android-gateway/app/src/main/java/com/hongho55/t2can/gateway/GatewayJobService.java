package com.hongho55.t2can.gateway;

import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public final class GatewayJobService extends JobService {
    private static final String TAG = "T2CAN-Gateway";
    private static final int JOB_ID = 0x5443;
    private static final long PERIOD_MS = 15L * 60L * 1000L;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private Future<?> currentRun;

    public static void schedule(Context context) {
        JobScheduler scheduler = context.getSystemService(JobScheduler.class);
        if (scheduler == null) {
            GatewayStatus.write(context, "scheduler_unavailable");
            return;
        }
        JobInfo job = new JobInfo.Builder(
                JOB_ID,
                new ComponentName(context, GatewayJobService.class))
                .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
                .setPeriodic(PERIOD_MS)
                .setBackoffCriteria(30_000L, JobInfo.BACKOFF_POLICY_EXPONENTIAL)
                .build();
        int result = scheduler.schedule(job);
        GatewayStatus.write(context, result == JobScheduler.RESULT_SUCCESS
                ? "scheduled"
                : "schedule_failed");
    }

    public static void runSync(Context context) {
        Context appContext = context.getApplicationContext();
        Thread worker = new Thread(() -> runSyncOnce(appContext), "t2can-manual-sync");
        worker.start();
    }

    private static void runSyncOnce(Context context) {
        TransferEngine.runOnce(context);
    }

    @Override
    public boolean onStartJob(JobParameters params) {
        currentRun = executor.submit(() -> {
            try {
                runSyncOnce(getApplicationContext());
            } finally {
                jobFinished(params, false);
            }
        });
        return true;
    }

    @Override
    public boolean onStopJob(JobParameters params) {
        if (currentRun != null) {
            currentRun.cancel(true);
        }
        return true;
    }

    @Override
    public void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }
}

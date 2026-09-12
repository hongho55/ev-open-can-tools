package com.hongho55.t2can.gateway;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.text.InputType;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private TextView statusView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (24 * getResources().getDisplayMetrics().density);
        root.setPadding(padding, padding, padding, padding);

        TextView title = new TextView(this);
        title.setText("T2CAN Gateway");
        title.setTextSize(24);
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView description = new TextView(this);
        description.setText("Read-only ESP32 incident collection.\n"
                + "The worker discovers EVCANTool and queues verified files.");
        description.setPadding(0, padding / 2, 0, padding / 2);
        root.addView(description, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        statusView = new TextView(this);
        root.addView(statusView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        EditText recorderUser = field("ESP32 read-only username", false);
        EditText recorderPassword = field("ESP32 read-only password", true);
        EditText uploadUrl = field("Mac HTTPS upload URL (optional)", false);
        EditText uploadToken = field("Mac upload token (optional)", true);
        root.addView(recorderUser);
        root.addView(recorderPassword);
        root.addView(uploadUrl);
        root.addView(uploadToken);

        try {
            SecureStore.Credentials saved = SecureStore.load(this);
            if (saved != null) {
                recorderUser.setText(saved.username);
                uploadUrl.setText(saved.uploadUrl);
            }
        } catch (Exception error) {
            GatewayStatus.write(this, "secure_store_error");
        }

        Button saveSettings = new Button(this);
        saveSettings.setText("Save private settings");
        saveSettings.setOnClickListener(view -> {
            try {
                SecureStore.Credentials previous = SecureStore.load(this);
                String password = recorderPassword.getText().toString();
                String token = uploadToken.getText().toString();
                if (previous != null && password.isEmpty()) {
                    password = previous.password;
                }
                if (previous != null && token.isEmpty()) {
                    token = previous.uploadToken;
                }
                SecureStore.save(this,
                        recorderUser.getText().toString(),
                        password,
                        uploadUrl.getText().toString(),
                        token);
                recorderPassword.setText("");
                uploadToken.setText("");
                GatewayStatus.write(this, "settings_saved");
                refreshStatus();
            } catch (Exception error) {
                GatewayStatus.write(this, "settings_error");
                refreshStatus();
            }
        });
        root.addView(saveSettings, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        Button discover = new Button(this);
        discover.setText("Sync ESP32 now");
        discover.setOnClickListener(view -> {
            GatewayStatus.write(this, "discovery_requested");
            refreshStatus();
            GatewayJobService.runSync(this);
            mainHandler.postDelayed(this::refreshStatus, 2200L);
        });
        root.addView(discover, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        Button schedule = new Button(this);
        schedule.setText("Enable periodic worker");
        schedule.setOnClickListener(view -> {
            GatewayJobService.schedule(this);
            refreshStatus();
        });
        root.addView(schedule, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        setContentView(root);
        GatewayJobService.schedule(this);
        refreshStatus();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshStatus();
    }

    private void refreshStatus() {
        if (statusView != null) {
            statusView.setText("Status: " + GatewayStatus.read(this));
        }
    }

    private EditText field(String hint, boolean password) {
        EditText field = new EditText(this);
        field.setHint(hint);
        field.setSingleLine(true);
        if (password) {
            field.setInputType(InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        } else {
            field.setInputType(InputType.TYPE_CLASS_TEXT);
        }
        return field;
    }
}

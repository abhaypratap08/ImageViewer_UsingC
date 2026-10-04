package com.abhaypratap.photon;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** SAF is the sole source of images; native code only ever sees private, bounded cache files. */
public class PhotonActivity extends SDLActivity {
    private static final int REQUEST_OPEN_IMAGE = 4101;
    private static final int REQUEST_OPEN_FOLDER = 4102;
    private static final int MAX_IMAGES = 128;
    private static final long MAX_FILE = 100L * 1024 * 1024;
    private static final long MAX_SESSION = 256L * 1024 * 1024;
    private static final String PREFS = "photon_documents";
    private static final String SESSION = "session";
    private static final String RECENTS = "recents";
    private static final ExecutorService IMPORTS = Executors.newSingleThreadExecutor();
    private static final String[] EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".webp"};
    private static volatile PhotonActivity instance;
    private static volatile String fontPath;
    private static volatile boolean immersive;
    private boolean pickerPending;

    @Override protected void onCreate(Bundle state) {
        instance = this;
        pickerPending = state != null && state.getBoolean("pickerPending");
        fontPath = copyFontAsset();
        super.onCreate(state);
        configureWindow();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putBoolean("pickerPending", pickerPending);
        super.onSaveInstanceState(state);
    }

    @Override protected void onDestroy() {
        if (instance == this) instance = null;
        super.onDestroy();
    }

    @Override protected String[] getLibraries() {
        return new String[] {"SDL2", "SDL2_image", "SDL2_ttf", "main"};
    }

    @Override protected void onOpenWithIntent(Intent intent) {
        if (intent != null && intent.getData() != null) {
            ArrayList<Uri> uris = new ArrayList<>();
            uris.add(intent.getData());
            enqueueImport(uris, "Open with", intent.getFlags());
        }
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        onOpenWithIntent(intent);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_OPEN_IMAGE && requestCode != REQUEST_OPEN_FOLDER) return;
        pickerPending = false;
        if (resultCode != Activity.RESULT_OK) {
            report("Selection cancelled.");
            return;
        }
        if (data == null) { report("No documents were selected."); return; }
        if (requestCode == REQUEST_OPEN_FOLDER) {
            Uri tree = data.getData();
            if (tree == null) { report("No folder was selected."); return; }
            persistGrant(tree, data.getFlags());
            final Context context = getApplicationContext();
            IMPORTS.execute(() -> importFolder(context, tree));
        } else {
            ArrayList<Uri> uris = new ArrayList<>();
            ClipData clips = data.getClipData();
            if (clips != null) {
                for (int i = 0; i < clips.getItemCount() && uris.size() < MAX_IMAGES + 1; i++)
                    if (clips.getItemAt(i).getUri() != null) uris.add(clips.getItemAt(i).getUri());
            } else if (data.getData() != null) uris.add(data.getData());
            if (uris.isEmpty()) { report("No images were selected."); return; }
            for (Uri uri : uris) persistGrant(uri, data.getFlags());
            enqueueImport(uris, "Documents", data.getFlags());
        }
    }

    private void persistGrant(Uri uri, int flags) {
        int access = flags & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if (access == 0 || (flags & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) == 0) return;
        try { getContentResolver().takePersistableUriPermission(uri, access); }
        catch (SecurityException ignored) { /* Transient grants still allow this import. */ }
    }

    @Override public void onBackPressed() {
        if (!mBrokenLibraries && nativeBackPressed()) return;
        super.onBackPressed();
    }

    /** Native closes the viewer by calling this only after it chooses to exit. */
    public static void finishViewer() {
        PhotonActivity activity = instance;
        if (activity != null) activity.runOnUiThread(() -> {
            if (instance == activity && !activity.isFinishing()) activity.finish();
        });
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) applySystemUi(immersive);
    }

    public static String getFontPath() { return fontPath; }

    public static void openPicker(boolean folder) {
        PhotonActivity activity = instance;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (instance != activity || activity.isFinishing() || activity.pickerPending) return;
            Intent intent = folder ? new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                    : new Intent(Intent.ACTION_OPEN_DOCUMENT);
            if (!folder) {
                intent.setType("image/*");
                intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
            }
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            try {
                activity.pickerPending = true;
                activity.startActivityForResult(intent, folder ? REQUEST_OPEN_FOLDER : REQUEST_OPEN_IMAGE);
            } catch (RuntimeException error) {
                activity.pickerPending = false;
                report("No document picker is available.");
            }
        });
    }

    public static void setPhotonImmersive(boolean enabled) {
        immersive = enabled;
        PhotonActivity activity = instance;
        if (activity != null) activity.runOnUiThread(() -> {
            if (instance == activity) activity.applySystemUi(enabled);
        });
    }

    /** Returns density and safe insets in physical screen pixels: density,left,top,right,bottom. */
    public static int[] getDisplayMetrics() {
        PhotonActivity activity = instance;
        if (activity == null) return new int[] {160, 0, 0, 0, 0};
        int dpi = activity.getResources().getDisplayMetrics().densityDpi;
        View decor = activity.getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30 && decor.getRootWindowInsets() != null) {
            WindowInsets insets = decor.getRootWindowInsets();
            android.graphics.Insets bars = insets.getInsetsIgnoringVisibility(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            return new int[] {dpi, bars.left, bars.top, bars.right, bars.bottom};
        }
        Rect visible = new Rect();
        decor.getWindowVisibleDisplayFrame(visible);
        int width = activity.getResources().getDisplayMetrics().widthPixels;
        int height = activity.getResources().getDisplayMetrics().heightPixels;
        return new int[] {dpi, Math.max(0, visible.left), Math.max(0, visible.top),
                Math.max(0, width - visible.right), Math.max(0, height - visible.bottom)};
    }

    private void configureWindow() {
        Window window = getWindow();
        window.setStatusBarColor(android.graphics.Color.BLACK);
        window.setNavigationBarColor(android.graphics.Color.BLACK);
        // Do not draw content behind cutouts or system bars in the non-immersive state.
        if (Build.VERSION.SDK_INT >= 30) window.setDecorFitsSystemWindows(true);
    }

    private void applySystemUi(boolean hide) {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(!hide);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                if (hide) controller.hide(WindowInsets.Type.systemBars());
                else controller.show(WindowInsets.Type.systemBars());
            }
        } else {
            int flags = hide ? View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                    View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION : 0;
            window.getDecorView().setSystemUiVisibility(flags);
        }
    }

    private String copyFontAsset() {
        File directory = new File(getCacheDir(), "photon-fonts");
        File target = new File(directory, "DejaVuSans.ttf");
        if (target.isFile() && target.length() > 0) return target.getAbsolutePath();
        if (!directory.isDirectory() && !directory.mkdirs()) return null;
        try (InputStream input = getAssets().open("fonts/DejaVuSans.ttf");
             FileOutputStream output = new FileOutputStream(target)) {
            byte[] buffer = new byte[8192];
            int n;
            while ((n = input.read(buffer)) != -1) output.write(buffer, 0, n);
            return target.getAbsolutePath();
        } catch (IOException error) { target.delete(); return null; }
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** Newline-separated cache paths, in selection order. Old entries are ignored if cache was evicted. */
    public static String getLastSessionPaths() {
        PhotonActivity activity = instance;
        if (activity == null) return "";
        StringBuilder paths = new StringBuilder();
        try {
            JSONArray items = new JSONArray(prefs(activity).getString(SESSION, "[]"));
            for (int i = 0; i < items.length() && i < MAX_IMAGES; i++) {
                String path = items.getJSONObject(i).optString("path");
                if (isSessionFile(activity, path)) paths.append(path).append('\n');
            }
        } catch (JSONException ignored) { }
        return paths.toString();
    }

    /** Display names and source locations for cache paths; never treat this as a filesystem path. */
    public static String getDocumentName(String path) { return sessionField(path, "name"); }
    public static String getDocumentLocation(String path) { return sessionField(path, "location"); }

    private static String sessionField(String path, String field) {
        PhotonActivity activity = instance;
        if (activity == null || !isSessionFile(activity, path)) return "";
        try {
            JSONArray items = new JSONArray(prefs(activity).getString(SESSION, "[]"));
            for (int i = 0; i < items.length(); i++) {
                JSONObject item = items.getJSONObject(i);
                if (path.equals(item.optString("path"))) return item.optString(field);
            }
        } catch (JSONException ignored) { }
        return "";
    }

    /** JSON array of {name,location,uri}; read only, max 32 newest first. */
    public static String getRecents() {
        PhotonActivity activity = instance;
        return activity == null ? "[]" : prefs(activity).getString(RECENTS, "[]");
    }
    public static void clearRecents() {
        PhotonActivity activity = instance;
        if (activity != null) prefs(activity).edit().remove(RECENTS).apply();
    }

    private static boolean isSessionFile(Context context, String path) {
        if (path == null || path.isEmpty()) return false;
        try {
            File root = new File(context.getCacheDir(), "photon-sessions").getCanonicalFile();
            File file = new File(path).getCanonicalFile();
            return file.isFile() && file.getPath().startsWith(root.getPath() + File.separator);
        } catch (IOException ignored) { return false; }
    }

    private static void enqueueImport(ArrayList<Uri> uris, String location, int flags) {
        PhotonActivity activity = instance;
        if (activity == null) return;
        Context context = activity.getApplicationContext();
        IMPORTS.execute(() -> importUris(context, uris, location));
    }

    private static void importFolder(Context context, Uri tree) {
        ArrayList<Uri> uris = new ArrayList<>();
        String location = safeName(displayName(context.getContentResolver(), tree));
        if (location.equals("image")) location = "Selected folder";
        try {
            String treeId = DocumentsContract.getTreeDocumentId(tree);
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, treeId);
            String[] projection = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_DISPLAY_NAME};
            try (Cursor cursor = context.getContentResolver().query(children, projection, null, null, null)) {
                if (cursor == null) { report("Unable to read this folder."); return; }
                int id = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
                int mime = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE);
                int name = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
                if (id < 0) { report("Unable to read this folder."); return; }
                // One level only: never traverse an arbitrary provider's entire tree.
                while (cursor.moveToNext() && uris.size() <= MAX_IMAGES) {
                    String type = mime >= 0 ? cursor.getString(mime) : null;
                    String filename = name >= 0 ? cursor.getString(name) : null;
                    String docId = cursor.getString(id);
                    if (docId != null && !DocumentsContract.Document.MIME_TYPE_DIR.equals(type) &&
                            ((type != null && type.startsWith("image/")) || extension(filename) != null))
                        uris.add(DocumentsContract.buildDocumentUriUsingTree(tree, docId));
                }
            }
        } catch (RuntimeException error) { report("Unable to read this folder."); return; }
        // Sort top-level entries for deterministic navigation; document picker preserves user order.
        Collections.sort(uris, Comparator.comparing(Uri::toString));
        if (uris.isEmpty()) { report("No supported images in this folder (subfolders are not scanned)."); return; }
        importUris(context, uris, location);
    }

    private static void importUris(Context context, ArrayList<Uri> uris, String location) {
        File root = new File(context.getCacheDir(), "photon-sessions");
        if (!root.isDirectory() && !root.mkdirs()) { report("Unable to create an image session."); return; }
        File session = new File(root, "session-" + System.currentTimeMillis() + "-" + Long.toHexString(Double.doubleToLongBits(Math.random())));
        if (!session.mkdir()) { report("Unable to create an image session."); return; }
        JSONArray items = new JSONArray();
        long used = 0;
        int skipped = 0;
        int limit = Math.min(uris.size(), MAX_IMAGES);
        for (int i = 0; i < limit; i++) {
            Uri uri = uris.get(i);
            try {
                String name = safeName(displayName(context.getContentResolver(), uri));
                String suffix = extension(name);
                if (suffix == null) suffix = mimeExtension(context.getContentResolver().getType(uri));
                if (suffix == null) { skipped++; continue; }
                // Numeric prefix keeps picker order even in native path-based sorting.
                File file = new File(session, String.format(Locale.ROOT, "%03d-%s", i, name));
                if (extension(file.getName()) == null) file = new File(session, file.getName() + suffix);
                try (InputStream input = context.getContentResolver().openInputStream(uri);
                     FileOutputStream output = new FileOutputStream(file)) {
                    if (input == null) throw new IOException("Unreadable document");
                    byte[] buffer = new byte[64 * 1024];
                    long size = 0;
                    int n;
                    while ((n = input.read(buffer)) != -1) {
                        size += n;
                        if (size > MAX_FILE || used + size > MAX_SESSION) throw new IOException("Import size limit");
                        output.write(buffer, 0, n);
                    }
                    if (size == 0) throw new IOException("Empty document");
                    used += size;
                } catch (Exception error) { file.delete(); throw error; }
                JSONObject item = new JSONObject();
                item.put("path", file.getAbsolutePath());
                item.put("name", name);
                item.put("location", location);
                item.put("uri", uri.toString());
                items.put(item);
            } catch (Exception error) { skipped++; }
        }
        if (items.length() == 0) {
            deleteTree(session);
            report("No readable supported images found (128 images, 100 MB each, 256 MB total limit).");
            return;
        }
        // commit is synchronous: a native event can immediately query its metadata.
        SharedPreferences settings = prefs(context);
        settings.edit().putString(SESSION, items.toString()).commit();
        addRecents(settings, items);
        cleanOldSessions(root, session);
        StringBuilder paths = new StringBuilder();
        for (int i = 0; i < items.length(); i++) paths.append(items.optJSONObject(i).optString("path")).append('\n');
        PhotonActivity current = instance;
        if (current != null && !mBrokenLibraries) nativeDocumentSelected(paths.toString());
        if (skipped > 0 || uris.size() > MAX_IMAGES)
            report("Imported " + items.length() + " images; " + (skipped + Math.max(0, uris.size() - MAX_IMAGES)) + " skipped (limits or unreadable).");
    }

    private static void addRecents(SharedPreferences settings, JSONArray items) {
        JSONArray recent = new JSONArray();
        HashSet<String> seen = new HashSet<>();
        try {
            JSONArray previous = new JSONArray(settings.getString(RECENTS, "[]"));
            for (int i = items.length() - 1; i >= 0; i--) {
                JSONObject item = items.getJSONObject(i);
                String uri = item.optString("uri");
                if (seen.add(uri)) {
                    JSONObject entry = new JSONObject();
                    entry.put("uri", uri); entry.put("name", item.optString("name"));
                    entry.put("location", item.optString("location"));
                    recent.put(entry);
                }
            }
            for (int i = 0; i < previous.length() && recent.length() < 32; i++) {
                JSONObject item = previous.getJSONObject(i);
                if (seen.add(item.optString("uri"))) recent.put(item);
            }
        } catch (JSONException ignored) { }
        settings.edit().putString(RECENTS, recent.toString()).apply();
    }

    private static void cleanOldSessions(File root, File keep) {
        File[] dirs = root.listFiles();
        if (dirs == null) return;
        for (File dir : dirs) {
            try {
                if (!dir.equals(keep) && dir.getName().startsWith("session-") &&
                        dir.isDirectory() && dir.getCanonicalFile().equals(dir.getAbsoluteFile()))
                    deleteTree(dir);
            } catch (IOException ignored) { }
        }
    }
    private static void deleteTree(File dir) {
        File[] files = dir.listFiles();
        if (files != null) for (File file : files) {
            try {
                if (file.isFile() && file.getCanonicalFile().getParentFile().equals(dir.getCanonicalFile()))
                    file.delete();
            } catch (IOException ignored) { }
        }
        dir.delete();
    }

    private static String displayName(ContentResolver resolver, Uri uri) {
        try (Cursor cursor = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && cursor.getString(0) != null) return cursor.getString(0);
        } catch (RuntimeException ignored) { }
        return uri.getLastPathSegment() == null ? "image" : uri.getLastPathSegment();
    }
    private static String safeName(String name) {
        if (name == null) return "image";
        name = name.replace('\\', '/');
        name = name.substring(name.lastIndexOf('/') + 1);
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < name.length() && out.length() < 96; i++) {
            char c = name.charAt(i);
            if (Character.isLetterOrDigit(c) || c == ' ' || c == '_' || c == '-' || c == '.') out.append(c);
            else if (!Character.isISOControl(c)) out.append('_');
        }
        String clean = out.toString().replace("..", ".").trim();
        while (clean.startsWith(".")) clean = clean.substring(1);
        return clean.isEmpty() ? "image" : clean;
    }
    private static String extension(String name) {
        if (name == null) return null;
        String lower = name.toLowerCase(Locale.ROOT);
        for (String suffix : EXTENSIONS) if (lower.endsWith(suffix)) return suffix;
        return null;
    }
    private static String mimeExtension(String type) {
        if (type == null) return null;
        switch (type) {
            case "image/png": return ".png";
            case "image/jpeg": return ".jpg";
            case "image/bmp": return ".bmp";
            case "image/gif": return ".gif";
            case "image/webp": return ".webp";
            default: return null;
        }
    }

    public static void copyImage(String path) {
        PhotonActivity activity = instance;
        if (activity == null || !isSessionFile(activity, path)) { report("Image is no longer available."); return; }
        activity.runOnUiThread(() -> {
            if (instance != activity) return;
            try {
                Uri uri = PhotonFileProvider.uriForFile(activity, new File(path));
                ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
                ClipData clip = new ClipData("Photon image", new String[] {"image/*"}, new ClipData.Item(uri));
                clipboard.setPrimaryClip(clip);
                // Android clipboard grants read permission to the paste target for content URIs.
            } catch (RuntimeException error) { report("Unable to copy this image."); }
        });
    }

    public static void shareImage(String path) {
        PhotonActivity activity = instance;
        if (activity == null || !isSessionFile(activity, path)) { report("Image is no longer available."); return; }
        activity.runOnUiThread(() -> {
            if (instance != activity) return;
            try {
                Uri uri = PhotonFileProvider.uriForFile(activity, new File(path));
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("image/*");
                send.putExtra(Intent.EXTRA_STREAM, uri);
                send.setClipData(ClipData.newUri(activity.getContentResolver(), "Photon image", uri));
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                activity.startActivity(Intent.createChooser(send, "Share image"));
            } catch (RuntimeException error) { report("Unable to share this image."); }
        });
    }

    /** Only a user-confirmed, write-granted SAF document may be deleted at source.
     * Returns false if no suitable grant exists. This never deletes a cache file. */
    public static boolean requestDeleteDocument(String path) {
        PhotonActivity activity = instance;
        if (activity == null || !isSessionFile(activity, path)) return false;
        String source = sessionField(path, "uri");
        if (source.isEmpty()) return false;
        Uri uri = Uri.parse(source);
        if (!DocumentsContract.isDocumentUri(activity, uri)) return false;
        boolean granted = false;
        for (android.content.UriPermission grant : activity.getContentResolver().getPersistedUriPermissions()) {
            if (grant.isWritePermission() && (grant.getUri().equals(uri) ||
                    (DocumentsContract.isTreeUri(grant.getUri()) &&
                     grant.getUri().getAuthority().equals(uri.getAuthority()) &&
                     DocumentsContract.getDocumentId(uri).startsWith(
                             DocumentsContract.getTreeDocumentId(grant.getUri()) + "/")))) {
                granted = true;
                break;
            }
        }
        if (!granted) return false;
        activity.runOnUiThread(() -> {
            if (instance != activity) return;
            new AlertDialog.Builder(activity).setTitle("Delete original document?")
                    .setMessage("Permanently delete the selected document from its provider? This cannot be undone.")
                    .setNegativeButton("Cancel", null)
                    .setPositiveButton("Delete", (dialog, which) -> IMPORTS.execute(() -> {
                        try {
                            if (DocumentsContract.deleteDocument(activity.getContentResolver(), uri))
                                report("Original document deleted. Remove it from this session to update the viewer.");
                            else report("Provider refused to delete the document.");
                        } catch (Exception error) { report("Provider refused to delete the document."); }
                    })).show();
        });
        return true;
    }

    private static void report(String message) {
        PhotonActivity activity = instance;
        if (activity != null) activity.runOnUiThread(() -> {
            if (instance == activity) {
                android.widget.Toast.makeText(activity, message, android.widget.Toast.LENGTH_LONG).show();
                if (!mBrokenLibraries) nativeDocumentMessage(message);
            }
        });
    }

    public static native void nativeDocumentSelected(String paths);
    public static native void nativeDocumentMessage(String message);
    public static native boolean nativeBackPressed();
}

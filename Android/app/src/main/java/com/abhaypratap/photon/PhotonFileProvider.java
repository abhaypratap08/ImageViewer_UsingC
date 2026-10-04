package com.abhaypratap.photon;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;

public final class PhotonFileProvider extends ContentProvider {
    private static final String AUTHORITY = "com.abhaypratap.photon.fileprovider";

    public static Uri uriForFile(Context context, File file) {
        try {
            String cache = context.getCacheDir().getCanonicalPath();
            String absolute = file.getCanonicalPath();
            if (!absolute.startsWith(cache + File.separator)) throw new IllegalArgumentException("Outside cache");
            String relative = absolute.substring(cache.length() + 1);
            return new Uri.Builder().scheme("content").authority(AUTHORITY)
                    .appendPath("cache").appendEncodedPath(relative).build();
        } catch (Exception error) {
            throw new IllegalArgumentException("Invalid Photon cache file", error);
        }
    }

    private File fileForUri(Uri uri) throws FileNotFoundException {
        java.util.List<String> segments = uri.getPathSegments();
        if (segments.size() < 3 || !"cache".equals(segments.get(0)))
            throw new FileNotFoundException("Unsupported Photon URI");
        File file = getContext().getCacheDir();
        for (int i = 1; i < segments.size(); i++) file = new File(file, segments.get(i));
        try {
            String cache = getContext().getCacheDir().getCanonicalPath();
            String candidate = file.getCanonicalPath();
            if (!candidate.startsWith(cache + File.separator))
                throw new FileNotFoundException("Outside cache");
        } catch (Exception error) {
            throw new FileNotFoundException("Invalid cache path");
        }
        if (!file.isFile()) throw new FileNotFoundException("Missing image");
        return file;
    }

    @Override public boolean onCreate() { return true; }
    @Override public String getType(Uri uri) {
        String extension = MimeTypeMap.getFileExtensionFromUrl(uri.toString());
        String type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
        return type == null ? "application/octet-stream" : type;
    }
    @Override public ParcelFileDescriptor openFile(Uri uri, String mode)
            throws FileNotFoundException {
        if (!"r".equals(mode)) throw new FileNotFoundException("Read only");
        return ParcelFileDescriptor.open(fileForUri(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }
    @Override public Cursor query(Uri uri, String[] projection, String selection,
                                   String[] selectionArgs, String sortOrder) {
        File file;
        try { file = fileForUri(uri); } catch (FileNotFoundException error) { return null; }
        String[] columns = projection == null ?
                new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE} : projection;
        MatrixCursor cursor = new MatrixCursor(columns, 1);
        Object[] values = new Object[columns.length];
        for (int i = 0; i < columns.length; i++) {
            if (OpenableColumns.DISPLAY_NAME.equals(columns[i])) values[i] = file.getName();
            else if (OpenableColumns.SIZE.equals(columns[i])) values[i] = file.length();
        }
        cursor.addRow(values);
        return cursor;
    }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) { return 0; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
}

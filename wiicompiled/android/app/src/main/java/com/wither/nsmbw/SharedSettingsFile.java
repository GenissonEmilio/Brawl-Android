// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import java.io.*;
import java.nio.channels.FileLock;
import java.nio.file.*;
import java.util.Map;
import java.util.HashMap;
import java.util.EnumSet;
import java.util.function.Consumer;
import java.util.Properties;

/** Every operation reloads under an inter-process lock. Atomic replacement preserves old data on failure. */
final class SharedSettingsFile {
    private static final Object PROCESS_LOCK = new Object();
    private final File file;
    SharedSettingsFile(File file, Map<String, ?> legacy) {
        this.file = file;
        access(values -> { if (!file.exists()) {
            for (Map.Entry<String, ?> entry : legacy.entrySet()) values.setProperty(entry.getKey(), String.valueOf(entry.getValue()));
            write(values);
        } return null; });
    }
    Map<String, String> snapshot() {
        return access(values -> {
            Map<String,String> copy=new HashMap<>();
            for(String key:values.stringPropertyNames()) copy.put(key,values.getProperty(key));
            return copy;
        });
    }
    String get(String key, String fallback) { return access(values -> values.getProperty(key, fallback)); }
    void put(String key, Object value) { access(values -> { values.setProperty(key, String.valueOf(value)); write(values); return null; }); }
    private interface Operation<T> { T run(Properties values) throws IOException; }
    private <T> T access(Operation<T> operation) {
        synchronized (PROCESS_LOCK) {
            try (RandomAccessFile lock = new RandomAccessFile(file + ".lock", "rw");
                 FileLock held = lock.getChannel().lock()) {
                Properties values = new Properties();
                if (file.exists()) try (InputStream in = new FileInputStream(file)) { values.load(in); }
                return operation.run(values);
            } catch (IOException failure) { throw new IllegalStateException("Cannot access player settings", failure); }
        }
    }
    private void write(Properties values) throws IOException {
        Path temporary = Files.createTempFile(file.getParentFile().toPath(), "settings-", ".tmp");
        try {
            try (FileOutputStream out = new FileOutputStream(temporary.toFile())) {
                values.store(out, "Brawl Android settings"); out.getFD().sync();
            }
            Files.move(temporary, file.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally { Files.deleteIfExists(temporary); }
    }
    /** App-facing access: failed writes never become confirmed state or terminate the UI. */
    static final class SafeAccess {
        private enum Operation { READ, WRITE }
        private final File file;
        private final Map<String,?> legacy;
        private final Map<String,String> confirmed=new HashMap<>();
        private final EnumSet<Operation> failures=EnumSet.noneOf(Operation.class);
        private final Consumer<IllegalStateException> report;
        private SharedSettingsFile store;
        SafeAccess(File file,Map<String,?> legacy,Consumer<IllegalStateException> report) {
            this.file=file;this.legacy=new HashMap<>(legacy);this.report=report;
            for(Map.Entry<String,?> entry:legacy.entrySet()) confirmed.put(entry.getKey(),String.valueOf(entry.getValue()));
            try {confirmed.putAll(store().snapshot());}
            catch(IllegalStateException failure) {failed(Operation.READ,failure);}
        }
        private SharedSettingsFile store() {
            if(store==null) store=new SharedSettingsFile(file,legacy);
            return store;
        }
        synchronized String get(String key,String fallback) {
            try {
                String value=store().get(key,fallback);
                confirmed.put(key,value);failures.remove(Operation.READ);
                return value;
            } catch(IllegalStateException failure) {
                failed(Operation.READ,failure);
                return confirmed.getOrDefault(key,fallback);
            }
        }
        synchronized boolean put(String key,Object value) {
            try {
                store().put(key,value);
                confirmed.put(key,String.valueOf(value));failures.remove(Operation.WRITE);
                return true;
            } catch(IllegalStateException failure) {failed(Operation.WRITE,failure);return false;}
        }
        private void failed(Operation operation,IllegalStateException failure) {
            boolean notify=failures.isEmpty();failures.add(operation);
            // Keep reporting once until all failing operation types recover. A successful read
            // alone must not re-enable repeated notifications for a persistently failing write.
            if(notify) report.accept(failure);
        }
    }

}

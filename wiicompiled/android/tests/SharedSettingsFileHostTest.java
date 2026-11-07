// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.TimeUnit;

public final class SharedSettingsFileHostTest {
    private static void check(boolean ok,String message) {if(!ok) throw new AssertionError(message);}
    private static void recoverableFailures(Path directory) throws Exception {
        Path path=directory.resolve("safe.properties");
        java.util.concurrent.atomic.AtomicInteger notices=new java.util.concurrent.atomic.AtomicInteger();
        SharedSettingsFile.SafeAccess safe=new SharedSettingsFile.SafeAccess(path.toFile(),Map.of("fps",true),failure -> notices.incrementAndGet());
        check(safe.get("fps","").equals("true"),"initial confirmed legacy value available");
        byte[] saved=Files.readAllBytes(path);
        Path lock=Path.of(path+".lock");Files.delete(lock);Files.createDirectory(lock);
        check(!safe.put("fps",false),"failed write explicitly reports false");
        check(safe.get("fps","").equals("true"),"failed write cannot replace confirmed value");
        check(!safe.put("fps",false) && safe.get("fps","").equals("true"),"repeated I/O errors preserve last saved state");
        check(notices.get()==1,"one notice across repeated read/write errors");
        check(Arrays.equals(saved,Files.readAllBytes(path)),"failed writes preserve persisted bytes");
        Files.delete(lock);
        check(safe.get("fps","").equals("true"),"read recovers after invalid lock directory is removed");
        check(safe.put("fps",false),"write recovers without restarting process");
        check(safe.get("fps","").equals("false"),"successful write becomes confirmed");
        Files.delete(lock);Files.createDirectory(lock);
        check(!safe.put("fps",true) && notices.get()==2,"a new failure after recovery produces another notice");
        check(safe.get("fps","").equals("false"),"second failure keeps latest confirmed value");
        Files.delete(lock);

        Path invalidParent=directory.resolve("not-a-directory");Files.writeString(invalidParent,"blocker");
        notices.set(0);
        Path initial=invalidParent.resolve("settings.properties");
        SharedSettingsFile.SafeAccess uninitialized=new SharedSettingsFile.SafeAccess(initial.toFile(),Map.of("scale",1.5f),failure -> notices.incrementAndGet());
        check(uninitialized.get("scale","").equals("1.5"),"constructor I/O failure falls back to migrated confirmed settings");
        check(!uninitialized.put("scale",2f) && notices.get()==1,"failed initialization notifies once and never claims save");
        Files.delete(invalidParent);Files.createDirectory(invalidParent);
        check(uninitialized.get("scale","").equals("1.5"),"initialization retries after directory repair");
        check(uninitialized.put("scale",2f) && uninitialized.get("scale","").equals("2.0"),"repaired storage accepts and confirms new settings");
    }
    public static void main(String[] args) throws Exception {
        if(args.length>0) {
            SharedSettingsFile settings=new SharedSettingsFile(Path.of(args[0]).toFile(),Map.of());
            for(int i=0;i<30;i++) settings.put(args[1]+i,i);
            return;
        }
        Path dir=Files.createTempDirectory("brawl-settings-test-");
        java.util.List<Process> children=new java.util.ArrayList<>();
        try {
            recoverableFailures(dir);
            Path path=dir.resolve("settings.properties");
            SharedSettingsFile launcher=new SharedSettingsFile(path.toFile(),Map.of("show_fps",true,"resolution_scale",1.5f));
            SharedSettingsFile game=new SharedSettingsFile(path.toFile(),Map.of("show_fps",false));
            check(game.get("show_fps","").equals("true"),"legacy migration happens once");
            game.put("show_fps",false);check(launcher.get("show_fps","").equals("false"),"live reader sees other instance write");
            launcher.put("vsync",true);check(game.get("resolution_scale","").equals("1.5"),"writes preserve other keys");
            String java=Path.of(System.getProperty("java.home"),"bin",System.getProperty("os.name").startsWith("Windows")?"java.exe":"java").toString();
            String cp=System.getProperty("java.class.path");
            Process a=new ProcessBuilder(java,"-Xmx32m","-cp",cp,SharedSettingsFileHostTest.class.getName(),path.toString(),"a").inheritIO().start();
            children.add(a);
            Process b=new ProcessBuilder(java,"-Xmx32m","-cp",cp,SharedSettingsFileHostTest.class.getName(),path.toString(),"b").inheritIO().start();
            children.add(b);
            check(a.waitFor(30,TimeUnit.SECONDS) && b.waitFor(30,TimeUnit.SECONDS),"child processes finish within timeout");
            check(a.exitValue()==0 && b.exitValue()==0,"child processes succeeded");
            for(int i=0;i<30;i++) for(String prefix:List.of("a","b")) check(launcher.get(prefix+i,"").equals(String.valueOf(i)),"concurrent writes retained");
            check(launcher.get("show_fps","").equals("false"),"no stale snapshot overwrites");
            System.out.println("PASS: atomic settings migration, two-process coherence, recoverable initialization/read/write failures and notification deduplication");
        } finally {
            for(Process child:children) if(child.isAlive()) {child.destroyForcibly();child.waitFor(5,TimeUnit.SECONDS);}
            try(var paths=Files.walk(dir)) {for(Path p:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator) Files.delete(p);}
        }
    }
}

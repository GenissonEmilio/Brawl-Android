// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.concurrent.atomic.AtomicInteger;

public final class GameInstallTransactionHostTest {
    private static int checks,cases;
    private static void check(boolean value,String message) {++checks;if(!value)throw new AssertionError(message);}
    private interface Work {void run() throws Exception;}
    private static void rejects(Work work,String message) throws Exception {
        try {work.run();throw new AssertionError(message);} catch(IOException expected) {++checks;}
    }
    private static final class Crash extends Error {}
    private static final class Fixture implements AutoCloseable {
        final Path base=Files.createTempDirectory("brawl-import-test-");
        final File game=base.resolve("game").toFile(),staging=base.resolve("game.importing").toFile(),backup=base.resolve("game.backup").toFile();
        final GameInstallTransaction.Validator validator=root -> {
            File marker=new File(root,"marker");
            String name=marker.isFile()?Files.readString(marker.toPath()):"missing";
            if(!name.equals("old")&&!name.equals("new"))throw new IOException("invalid candidate fixture");
        };
        Fixture() throws IOException {
            write(game,"old");
            Files.createDirectories(base.resolve("NAND/title"));
            Files.writeString(base.resolve("NAND/title/save"),"player-save");
            Files.writeString(base.resolve("Config.toml"),"player-settings");
            Files.createDirectories(base.resolve("Cache"));
            Files.writeString(base.resolve("Cache/cache"),"keep-cache");
        }
        File candidate(String name) throws IOException {File c=new File(staging,"DATA");write(c,name);return c;}
        static void write(File root,String value) throws IOException {Files.createDirectories(root.toPath());Files.writeString(new File(root,"marker").toPath(),value);}
        String marker(File root) throws IOException {return Files.readString(new File(root,"marker").toPath());}
        void unrelatedPreserved() throws IOException {
            check(Files.readString(base.resolve("NAND/title/save")).equals("player-save"),"NAND untouched");
            check(Files.readString(base.resolve("Config.toml")).equals("player-settings"),"Config untouched");
            check(Files.readString(base.resolve("Cache/cache")).equals("keep-cache"),"unrelated cache untouched");
        }
        @Override public void close() throws IOException {
            try(var paths=Files.walk(base)) {for(Path p:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator) Files.delete(p);}
        }
    }
    private static void run(String name,Work work) throws Exception {work.run();++cases;System.out.println("PASS: "+name);}
    public static void main(String[] args) throws Exception {
        run("successful directory swap",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new");
            check(new GameInstallTransaction(f.base.toFile()).install(c,f.validator),"successful cleanup");
            check(f.marker(f.game).equals("new"),"new candidate published");check(!f.backup.exists(),"backup removed only after publish");check(!f.staging.exists(),"staging removed after commit");f.unrelatedPreserved();
        }});
        run("first installation publishes a top-level staging root",() -> {try(Fixture f=new Fixture()) {
            Files.delete(new File(f.game,"marker").toPath());Files.delete(f.game.toPath());
            Fixture.write(f.staging,"new");
            check(new GameInstallTransaction(f.base.toFile()).install(f.staging,f.validator),"first import succeeds");
            check(f.marker(f.game).equals("new")&&!f.staging.exists()&&!f.backup.exists(),"top-level staging becomes game without a previous install");f.unrelatedPreserved();
        }});
        run("first rename failure keeps installed directory",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new");AtomicInteger calls=new AtomicInteger();
            GameInstallTransaction tx=new GameInstallTransaction(f.base.toFile(),(a,b) -> calls.incrementAndGet()!=1&&a.renameTo(b));
            rejects(() -> tx.install(c,f.validator),"first rename must fail");
            check(f.marker(f.game).equals("old"),"old game still in place");check(!f.backup.exists()&&c.exists(),"candidate and old retained");f.unrelatedPreserved();
        }});
        run("second rename failure rolls old directory back",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new");AtomicInteger calls=new AtomicInteger();
            GameInstallTransaction tx=new GameInstallTransaction(f.base.toFile(),(a,b) -> calls.incrementAndGet()!=2&&a.renameTo(b));
            rejects(() -> tx.install(c,f.validator),"publish rename must fail");
            check(calls.get()==3,"rollback rename attempted");check(f.marker(f.game).equals("old"),"old directory restored");check(!f.backup.exists()&&c.exists(),"candidate retained and rollback completed");f.unrelatedPreserved();
        }});
        run("failed rollback retains backup for startup",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new");AtomicInteger calls=new AtomicInteger();
            GameInstallTransaction tx=new GameInstallTransaction(f.base.toFile(),(a,b) -> {int n=calls.incrementAndGet();return n!=2&&n!=3&&a.renameTo(b);});
            rejects(() -> tx.install(c,f.validator),"publish and rollback must fail");
            check(!f.game.exists()&&f.marker(f.backup).equals("old"),"backup survives failed rollback");
            new GameInstallTransaction(f.base.toFile()).recover(f.validator);
            check(f.marker(f.game).equals("old")&&!f.backup.exists(),"next startup restores backup");f.unrelatedPreserved();
        }});
        run("process death after old rename recovers on startup",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new");AtomicInteger calls=new AtomicInteger();
            GameInstallTransaction tx=new GameInstallTransaction(f.base.toFile(),(a,b) -> {boolean ok=a.renameTo(b);if(calls.incrementAndGet()==1&&ok)throw new Crash();return ok;});
            try {tx.install(c,f.validator);throw new AssertionError("crash must occur");}catch(Crash expected) {++checks;}
            check(!f.game.exists()&&f.marker(f.backup).equals("old"),"old copy preserved across simulated death");
            new GameInstallTransaction(f.base.toFile()).recoverAtStartup(f.validator);
            check(f.marker(f.game).equals("old")&&!f.staging.exists(),"startup restores old then removes interrupted staging");f.unrelatedPreserved();
        }});
        run("process death after publish completes cleanup",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new");AtomicInteger calls=new AtomicInteger();
            GameInstallTransaction tx=new GameInstallTransaction(f.base.toFile(),(a,b) -> {boolean ok=a.renameTo(b);if(calls.incrementAndGet()==2&&ok)throw new Crash();return ok;});
            try {tx.install(c,f.validator);throw new AssertionError("crash must occur");}catch(Crash expected) {++checks;}
            check(f.marker(f.game).equals("new")&&f.marker(f.backup).equals("old"),"both copies survive until recovery");
            new GameInstallTransaction(f.base.toFile()).recoverAtStartup(f.validator);
            check(f.marker(f.game).equals("new")&&!f.backup.exists()&&!f.staging.exists(),"validated publication kept and temporary trees cleaned");f.unrelatedPreserved();
        }});
        run("invalid candidate never mutates old installation",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("invalid");AtomicInteger calls=new AtomicInteger();
            GameInstallTransaction tx=new GameInstallTransaction(f.base.toFile(),(a,b) -> {calls.incrementAndGet();return a.renameTo(b);});
            rejects(() -> tx.install(c,f.validator),"invalid candidate must fail validation");
            check(calls.get()==0&&f.marker(f.game).equals("old"),"validation precedes every rename");check(!f.backup.exists()&&c.exists(),"no old or candidate deletion");f.unrelatedPreserved();
        }});
        run("invalid publication restores validated backup",() -> {try(Fixture f=new Fixture()) {
            check(f.game.renameTo(f.backup),"prepare crash state");Fixture.write(f.game,"invalid");
            new GameInstallTransaction(f.base.toFile()).recover(f.validator);
            check(f.marker(f.game).equals("old")&&!f.backup.exists(),"invalid new tree replaced by old backup");f.unrelatedPreserved();
        }});
        run("invalid backup is never substituted or deleted",() -> {try(Fixture f=new Fixture()) {
            Fixture.write(f.game,"invalid");Fixture.write(f.backup,"invalid-backup");
            rejects(() -> new GameInstallTransaction(f.base.toFile()).recover(f.validator),"invalid backup cannot recover");
            check(f.marker(f.game).equals("invalid")&&f.marker(f.backup).equals("invalid-backup"),"both invalid trees preserved for diagnosis");f.unrelatedPreserved();
        }});
        run("candidate outside staging rejected",() -> {try(Fixture f=new Fixture()) {
            rejects(() -> new GameInstallTransaction(f.base.toFile()).install(f.base.resolve("NAND").toFile(),f.validator),"NAND is not an import candidate");
            check(f.marker(f.game).equals("old"),"out-of-scope import cannot mutate game");f.unrelatedPreserved();
        }});
        run("SHA256 checks bytes using independently known vector",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new"),dol=new File(c,"main.dol");Files.write(dol.toPath(),"abc".getBytes(StandardCharsets.US_ASCII));
            GameInstallTransaction.verifySha256(dol,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");++checks;
            rejects(() -> GameInstallTransaction.verifySha256(dol,"0000000000000000000000000000000000000000000000000000000000000000"),"incorrect executable digest must fail");f.unrelatedPreserved();
        }});
        run("checksum validator rejects before any directory move",() -> {try(Fixture f=new Fixture()) {
            File c=f.candidate("new"),dol=new File(c,"main.dol");Files.writeString(dol.toPath(),"abc");AtomicInteger calls=new AtomicInteger();
            GameInstallTransaction tx=new GameInstallTransaction(f.base.toFile(),(a,b) -> {calls.incrementAndGet();return a.renameTo(b);});
            GameInstallTransaction.Validator validate=root -> {f.validator.validate(root);GameInstallTransaction.verifySha256(new File(root,"main.dol"),"0000000000000000000000000000000000000000000000000000000000000000");};
            rejects(() -> tx.install(c,validate),"bad executable digest must reject the transaction");
            check(calls.get()==0&&f.marker(f.game).equals("old"),"checksum mismatch leaves old game in place");f.unrelatedPreserved();
        }});
        System.out.println("PASS: import transaction cases="+cases+" checks="+checks);
    }
}

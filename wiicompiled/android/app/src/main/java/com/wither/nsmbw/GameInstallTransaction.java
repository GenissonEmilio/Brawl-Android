// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Same-filesystem directory publication. No save/config paths belong to this transaction. */
final class GameInstallTransaction {
    interface Validator { void validate(File root) throws IOException; }
    interface Mover { boolean rename(File from, File to); }
    private static final Object PROCESS_LOCK=new Object();
    private final File base,game,staging,backup;
    private final Mover mover;

    GameInstallTransaction(File data) throws IOException {this(data,File::renameTo);}
    GameInstallTransaction(File data,Mover mover) throws IOException {
        base=data.getCanonicalFile();
        game=new File(base,"game");staging=new File(base,"game.importing");backup=new File(base,"game.backup");
        this.mover=mover;
        checkRoots();
    }
    boolean needsRecovery() {return backup.exists();}

    /** The lease spans copying, validation, publication and failure cleanup. Never wait on the UI. */
    ImportLease beginImport() throws IOException {
        RandomAccessFile handle = new RandomAccessFile(new File(base,"game.import.lock"),"rw");
        try {
            FileLock lock;
            try { lock=handle.getChannel().tryLock(); }
            catch(OverlappingFileLockException busy) { lock=null; }
            if(lock==null) throw new IOException("another game import or recovery is already running");
            return new ImportLease(handle,lock);
        } catch(IOException | RuntimeException failure) {handle.close();throw failure;}
    }
    static final class ImportLease implements AutoCloseable {
        private final RandomAccessFile handle; private final FileLock lock;
        ImportLease(RandomAccessFile handle,FileLock lock) {this.handle=handle;this.lock=lock;}
        @Override public void close() throws IOException {try {lock.release();} finally {handle.close();}}
    }
    void recoverAtStartup(Validator validator) throws IOException {
        try(ImportLease lease=beginImport()) {
            synchronized(PROCESS_LOCK) {
                recoverLocked(validator);
                // Also reclaim a copy interrupted before the first game -> backup rename.
                deleteOwned(staging);
            }
        }
    }

    /** Called off the UI thread before launching or importing. A process can die between any steps. */
    void recover(Validator validator) throws IOException {
        synchronized(PROCESS_LOCK) {recoverLocked(validator);}
    }

    private void recoverLocked(Validator validator) throws IOException {
        checkRoots();
        if(!backup.exists()) return;
        if(!game.exists()) {
            validator.validate(backup);
            move(backup,game,"cannot restore the previous game; it remains in game.backup");
            return;
        }
        boolean publishedValid=true;
        try {validator.validate(game);} catch(IOException invalid) {publishedValid=false;}
        if(publishedValid) {
            deleteOwned(backup);
        } else {
            // Keep the backup intact unless it can actually replace the damaged publication.
            validator.validate(backup);
            deleteOwned(game);
            move(backup,game,"cannot restore the previous game; it remains in game.backup");
        }
    }

    /** Returns false only when publication succeeded but old/staging cleanup must be retried. */
    boolean install(File candidate,Validator validator) throws IOException {
        synchronized(PROCESS_LOCK) {return installLocked(candidate,validator);}
    }

    private boolean installLocked(File candidate,Validator validator) throws IOException {
        checkRoots();
        File root=candidate.getCanonicalFile();
        if(!root.toPath().startsWith(staging.getCanonicalFile().toPath())||!root.isDirectory())
            throw new IOException("the import candidate is outside game.importing");
        // Validate before touching the installed game or an interrupted transaction.
        validator.validate(root);
        recoverLocked(validator);
        boolean hadGame=game.exists();
        if(hadGame) move(game,backup,"cannot preserve the existing game; nothing was replaced");
        try {
            move(root,game,"cannot publish the imported game");
        } catch(IOException publishFailure) {
            if(hadGame&&!game.exists()&&!mover.rename(backup,game)) {
                throw new IOException("cannot publish or restore the game; the previous game is safe in game.backup",publishFailure);
            }
            throw publishFailure;
        }
        // Commit is complete. A cleanup failure must not be reported as a failed/unchanged import.
        boolean cleaned=true;
        try {deleteOwned(backup);} catch(IOException pending) {cleaned=false;}
        try {deleteOwned(staging);} catch(IOException pending) {cleaned=false;}
        return cleaned;
    }

    void discardStaging() throws IOException {synchronized(PROCESS_LOCK) {deleteOwned(staging);}}

    private void move(File from,File to,String problem) throws IOException {
        if(to.exists()||!mover.rename(from,to)) throw new IOException(problem);
    }
    private void checkRoots() throws IOException {
        for(File path:new File[]{game,staging,backup}) {
            if(Files.isSymbolicLink(path.toPath())||!path.getCanonicalFile().getParentFile().equals(base))
                throw new IOException("unsafe game import path: "+path.getName());
        }
    }
    private void deleteOwned(File path) throws IOException {
        if(!path.equals(game)&&!path.equals(staging)&&!path.equals(backup))
            throw new IOException("refusing to remove an unrelated path");
        checkRoots();
        if(!path.exists()) return;
        // walkFileTree does not follow symbolic links inside game data.
        Files.walkFileTree(path.toPath(),new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult visitFile(Path file,BasicFileAttributes attr) throws IOException {
                Files.delete(file);return FileVisitResult.CONTINUE;
            }
            @Override public FileVisitResult postVisitDirectory(Path dir,IOException error) throws IOException {
                if(error!=null) throw error;
                Files.delete(dir);return FileVisitResult.CONTINUE;
            }
        });
    }

    static void verifySha256(File file,String expected) throws IOException {
        final MessageDigest digest;
        try {digest=MessageDigest.getInstance("SHA-256");}
        catch(NoSuchAlgorithmException impossible) {throw new IOException("SHA-256 is unavailable",impossible);}
        try(InputStream in=Files.newInputStream(file.toPath())) {
            byte[] block=new byte[1<<16];int count;
            while((count=in.read(block))!=-1) if(count>0) digest.update(block,0,count);
        }
        StringBuilder actual=new StringBuilder(64);
        for(byte b:digest.digest()) actual.append(Character.forDigit((b>>>4)&15,16)).append(Character.forDigit(b&15,16));
        if(!actual.toString().equalsIgnoreCase(expected))
            throw new IOException("sys/main.dol does not match the supported USA revision 1 executable; the existing game was retained");
    }
}

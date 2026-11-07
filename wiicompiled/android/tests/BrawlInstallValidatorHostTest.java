// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;
import java.io.IOException;
import java.nio.*;
import java.nio.file.*;
import java.util.Comparator;

public final class BrawlInstallValidatorHostTest {
    private interface Work {void run() throws Exception;}
    private static void rejects(Work work) throws Exception {try {work.run();throw new AssertionError("invalid install accepted");} catch(IOException expected) {}}
    private static byte[] fst(String name,int length) {
        byte[] text=(name+"\0").getBytes(java.nio.charset.StandardCharsets.UTF_8);
        ByteBuffer bytes=ByteBuffer.allocate(24+text.length).order(ByteOrder.BIG_ENDIAN);
        bytes.putInt(0x01000000).putInt(0).putInt(2).putInt(0).putInt(0).putInt(length).put(text);
        return bytes.array();
    }
    private static byte[] nestedFst() {
        byte[] names="dir\0asset\0empty\0".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        return ByteBuffer.allocate(48+names.length).order(ByteOrder.BIG_ENDIAN)
            .putInt(0x01000000).putInt(0).putInt(4)
            .putInt(0x01000000).putInt(0).putInt(3)
            .putInt(4).putInt(0).putInt(3)
            .putInt(0x0100000a).putInt(0).putInt(4).put(names).array();
    }
    public static void main(String[] args) throws Exception {
        Path root=Files.createTempDirectory("brawl-fst-test-");
        try {
            Files.createDirectories(root.resolve("sys"));Files.createDirectories(root.resolve("files"));
            Path manifest=root.resolve("sys/fst.bin"),asset=root.resolve("files/asset");
            Files.write(manifest,fst("asset",3));Files.write(asset,new byte[]{1,2,3});
            BrawlInstallValidator.validateFst(root.toFile());
            Files.write(asset,new byte[]{1,2});rejects(()->BrawlInstallValidator.validateFst(root.toFile()));
            Files.delete(asset);rejects(()->BrawlInstallValidator.validateFst(root.toFile()));
            Files.write(manifest,fst("../outside",0));rejects(()->BrawlInstallValidator.validateFst(root.toFile()));
            byte[] bad=fst("asset",3);ByteBuffer.wrap(bad).putInt(8,0x7fffffff);Files.write(manifest,bad);
            rejects(()->BrawlInstallValidator.validateFst(root.toFile()));
            Files.write(manifest,fst("asset",3));Files.write(asset,new byte[]{1,2,3});
            rejects(()->BrawlInstallValidator.validate(root.toFile())); // synthetic FST cannot replace Brawl's module corpus
            Files.createDirectories(root.resolve("files/dir"));Files.createDirectories(root.resolve("files/empty"));
            Path child=root.resolve("files/dir/asset");Files.write(child,new byte[]{1,2,3});
            Files.write(manifest,nestedFst());BrawlInstallValidator.validateFst(root.toFile());
            byte[] nested=nestedFst();ByteBuffer.wrap(nested).putInt(16,2);Files.write(manifest,nested);
            rejects(()->BrawlInstallValidator.validateFst(root.toFile())); // wrong parent index
            nested=nestedFst();ByteBuffer.wrap(nested).putInt(20,5);Files.write(manifest,nested);
            rejects(()->BrawlInstallValidator.validateFst(root.toFile())); // directory exceeds root
            nested=nestedFst();ByteBuffer.wrap(nested).putInt(36,0x01000000);Files.write(manifest,nested);
            rejects(()->BrawlInstallValidator.validateFst(root.toFile())); // duplicate canonical directory
            byte[] unterminated=fst("asset",3);unterminated[unterminated.length-1]=1;Files.write(manifest,unterminated);
            rejects(()->BrawlInstallValidator.validateFst(root.toFile()));
            Files.write(manifest,nestedFst());Files.delete(child);
            Path outside=root.resolve("outside");Files.write(outside,new byte[]{1,2,3});
            try {Files.createSymbolicLink(child,outside);rejects(()->BrawlInstallValidator.validateFst(root.toFile()));}
            catch(UnsupportedOperationException|java.nio.file.FileSystemException unsupported) {System.out.println("SKIP: host cannot create test symlink");}
            System.out.println("PASS: complete FST sizes, missing files, unsafe paths, malformed counts, nested/empty directories, parent/end bounds, duplicate paths, symlinks and missing Brawl modules");
        } finally {
            try(var paths=Files.walk(root)) {for(Path p:(Iterable<Path>)paths.sorted(Comparator.reverseOrder())::iterator) Files.delete(p);}
        }
    }
}

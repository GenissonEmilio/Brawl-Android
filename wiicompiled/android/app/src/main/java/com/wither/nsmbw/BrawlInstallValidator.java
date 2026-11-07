// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.CRC32;

/** Validates every disc file against sys/fst.bin and the public 126-module Brawl corpus. */
final class BrawlInstallValidator {
    private static void require(boolean condition, String message) throws IOException {
        if (!condition) throw new IOException("Incomplete or unsupported Brawl data: " + message);
    }
    private static long u32(byte[] bytes, int offset) {
        return ((bytes[offset]&255L)<<24)|((bytes[offset+1]&255L)<<16)|((bytes[offset+2]&255L)<<8)|(bytes[offset+3]&255L);
    }
    static void validate(File root) throws IOException {
        validateFst(root);
        for (String row : MODULES.split(" ")) {
            String[] item = row.split(":");
            File file = new File(root, "files/module/" + item[0] + ".rel");
            try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
                require(in.length() >= 0x4c, item[0] + " REL header");
                byte[] header = new byte[0x4c]; in.readFully(header);
                long fixed = Long.decode(item[2]);
                require(u32(header,0)==Long.parseLong(item[1]) && u32(header,0x1c)==3
                    && u32(header,0x48)==fixed && fixed>=0x4c && fixed<=in.length()
                    && u32(header,0x20)==Long.decode(item[3]), item[0] + " REL identity");
                CRC32 crc = new CRC32(); byte[] block = new byte[65536]; long remaining=fixed-0x4c;
                while (remaining>0) {int count=in.read(block,0,(int)Math.min(block.length,remaining));
                    require(count>0,item[0]+" truncated REL");crc.update(block,0,count);remaining-=count;}
                require(crc.getValue()==Long.decode(item[4]),item[0]+" REL checksum");
            }
        }
    }
    static void validateFst(File root) throws IOException {
        File manifest = new File(root,"sys/fst.bin");
        require(manifest.isFile() && manifest.length()>=12 && manifest.length()<=32*1024*1024,"sys/fst.bin missing or invalid");
        byte[] fst=Files.readAllBytes(manifest.toPath());
        long entryCount=u32(fst,8);
        require(fst[0]==1 && entryCount>1 && entryCount<=fst.length/12,"FST root");
        int count=(int)entryCount,names=count*12;
        require(!Files.isSymbolicLink(new File(root,"files").toPath()),"symlinked files root");
        File files=new File(root,"files").getCanonicalFile();
        Deque<File> dirs=new ArrayDeque<>();Deque<Integer> ends=new ArrayDeque<>();Deque<Integer> indexes=new ArrayDeque<>();
        dirs.push(files);ends.push(count);indexes.push(0);
        Set<String> seen=new HashSet<>();int fileCount=0;
        for(int i=1;i<count;i++) {
            while(i==ends.peek()) {dirs.pop();ends.pop();indexes.pop();}
            int offset=i*12, nameOffset=(int)(u32(fst,offset)&0xffffff)+names;
            require(nameOffset>=names && nameOffset<fst.length,"FST name offset");
            int end=nameOffset;while(end<fst.length && fst[end]!=0) end++;
            require(end<fst.length,"unterminated FST name");
            String name=new String(fst,nameOffset,end-nameOffset,StandardCharsets.UTF_8);
            require(!name.isEmpty()&&!name.equals(".")&&!name.equals("..")&&!name.contains("/")&&!name.contains("\\"),"unsafe FST name");
            File target=new File(dirs.peek(),name);
            require(!Files.isSymbolicLink(target.toPath()) && target.getCanonicalPath().startsWith(files.getPath()+File.separator)
                    && seen.add(target.getCanonicalPath()),"unsafe or duplicate FST path");
            if(fst[offset]==1) {
                int limit=(int)u32(fst,offset+8);
                require(u32(fst,offset+4)==indexes.peek() && limit>i && limit<=ends.peek() && target.isDirectory(),"FST directory "+name);
                dirs.push(target);ends.push(limit);indexes.push(i);
            } else {
                require(fst[offset]==0 && target.isFile() && target.length()==u32(fst,offset+8),"missing/truncated file "+target.getName());
                fileCount++;
            }
        }
        require(fileCount>0,"empty disc manifest");
    }
    // Generated from projects/brawl/module_layout.json, the same reference used by check-brawl-input.py.
    private static final String MODULES = "ft_captain:100:0x21d68:0x3cc:0x129542c5 ft_dedede:117:0x369f8:0x518:0x3d815f40 ft_diddy:115:0x2b6a8:0x448:0xd8d5d844 ft_donkey:92:0x21d78:0x394:0x8e64b12 ft_falco:108:0x1cef0:0x250:0x2683c87e ft_fox:97:0x1cf30:0x278:0x84ac6373 ft_gamewatch:107:0x35608:0x480:0xc5cc7803 ft_ganon:109:0x1d6b0:0x2e0:0xc2e3b21c ft_iceclimber:105:0x31eb8:0x50c:0x848f7103 ft_ike:119:0x1e9c0:0x320:0xac6144ba ft_kirby:96:0x46f60:0x1344:0x684a73e8 ft_koopa:102:0x29c30:0x628:0x12c76200 ft_link:93:0x271e0:0x2e4:0x10952ed1 ft_lucario:118:0x1f6c8:0x394:0x1a68e306 ft_lucas:114:0x33640:0x510:0xf8ab89d1 ft_luigi:99:0x1c0c8:0x2c0:0x18b49a23 ft_mario:91:0x29a18:0x410:0x53bf9f39 ft_marth:106:0x16720:0x1d8:0x6a03d017 ft_metaknight:111:0x1bc98:0x25c:0x2c30efe3 ft_ness:101:0x38a40:0x5a8:0xcf8f328c ft_peach:103:0x1ebc0:0x2a0:0x8b510290 ft_pikachu:98:0x2b5c0:0x488:0x1b8ee4db ft_pikmin:113:0x40570:0x390:0x572ce60b ft_pit:112:0x25930:0x314:0x43f3ae5d ft_poke:116:0x3b450:0x68c:0x9ce8983 ft_purin:124:0x156e8:0x260:0x1d8643af ft_robot:120:0x263a0:0x46c:0x50f96773 ft_samus:94:0x34298:0x488:0x665b7969 ft_snake:122:0x404d8:0x4e8:0x749b8f81 ft_sonic:123:0x25808:0x468:0xf5341653 ft_toonlink:121:0x27358:0x2e4:0xc7c95814 ft_wario:110:0x31048:0x5ec:0xbfec0486 ft_wolf:125:0x1cde0:0x278:0x24061b8 ft_yoshi:95:0x2df38:0x5fc:0xd2edb589 ft_zako:126:0x1b8d8:0x1e4:0xa93982be ft_zelda:104:0x39970:0x658:0x7cfe5ae0 sora_adv_menu_difficulty:32:0x2930:0x4:0x9349a0c5 sora_adv_menu_ending:37:0x35d0:0x4:0xbd32c2b sora_adv_menu_game_over:33:0x5520:0x18:0x9cf3ba9b sora_adv_menu_name:28:0x4d30:0x4:0x909cf91b sora_adv_menu_result:34:0x3388:0x18:0xd7bb4e53 sora_adv_menu_save_load:35:0x1a50:0x4:0xb8d86f65 sora_adv_menu_save_point:39:0x1a50:0x4:0x786105f3 sora_adv_menu_seal:36:0x1a50:0x4:0xc5ab693d sora_adv_menu_sel_char:30:0x1a50:0x4:0xf88de234 sora_adv_menu_sel_map:31:0x60e8:0x4:0x394c1fc6 sora_adv_menu_telop:38:0x2230:0x4:0xd4b25cdd sora_adv_menu_visual:29:0x4930:0x4:0x87115773 sora_adv_stage:40:0x6de40:0x2a10:0xca34ac7d sora_enemy:41:0x97700:0x860:0xdd6f8e7a sora_melee:27:0x47a480:0x8ce8:0x94960df0 sora_menu_boot:22:0x3f50:0x4:0xd8090914 sora_menu_challenger:23:0x24b0:0x4:0xcead04a1 sora_menu_collect_viewer:6:0xebb0:0x14:0xdd8fb25e sora_menu_edit:5:0x17a80:0x1d8:0x88e432ea sora_menu_event:9:0x5440:0x4:0x674edf3e sora_menu_fig_get_demo:26:0x3130:0x18:0xe3661dea sora_menu_friend_list:14:0xc1f0:0x4:0x5c2644f0 sora_menu_game_over:12:0x2ed0:0x4:0x7fa906b7 sora_menu_intro:13:0x38f0:0x4:0x2718a4be sora_menu_main:2:0x468a8:0x74:0x4a2233a5 sora_menu_name:16:0xcaf0:0xac:0x2ba16b21 sora_menu_qm:4:0xb9a0:0x4:0xcf8ee84a sora_menu_replay:7:0x3980:0x1d0:0xcbc4181a sora_menu_rule:18:0xcd70:0x4:0xd1cf6095 sora_menu_sel_char:10:0x21148:0x14:0xc9d0e85 sora_menu_sel_char_access:17:0x1ad0:0x4:0xe81b76d4 sora_menu_sel_stage:11:0xaab0:0x14:0xadafefaf sora_menu_simple_ending:19:0x30b0:0x4:0x2736a320 sora_menu_snap_shot:8:0x2bc0:0x1d0:0x35542698 sora_menu_time_result:21:0x28f0:0x4:0x6451d63 sora_menu_title:24:0x5830:0x4:0x568e18c sora_menu_title_sunset:25:0x5f30:0x4:0x8df8c12 sora_menu_tour:3:0x19dd0:0x4:0xb1c5223e sora_menu_watch:15:0x6fd0:0x1c:0xc4893f6a sora_minigame:20:0x10850:0x30:0xf87e4bc6 sora_scene:1:0x4d7b0:0x1c08:0xfc172d49 st_battle:43:0x3880:0x18:0xb4d75b47 st_battles:42:0x2e58:0x18:0xd94953d1 st_config:44:0x2cc0:0x18:0xaf5ce6ef st_crayon:56:0x112a8:0x160:0xedf8fd78 st_croll:90:0xd3f8:0x2c:0x3f1223f1 st_dolpic:46:0xc798:0x18:0x87d1dd49 st_donkey:50:0x126c8:0x128:0x2474574e st_dxbigblue:81:0xda78:0x18:0x14726e8a st_dxcorneria:82:0x7690:0x58:0x80d1dacc st_dxgarden:77:0xb800:0x128:0xfdf6f1ab st_dxgreens:79:0x122c0:0x150:0x25da2037 st_dxonett:78:0xe6d0:0x128:0x5c7aaa94 st_dxpstadium:83:0x5a78:0x18:0x65bb765c st_dxrcruise:80:0x5920:0x18:0x191af7d7 st_dxshrine:75:0x2d70:0x18:0xef49e86f st_dxyorster:76:0xcd58:0x48:0x382fbd3a st_dxzebes:84:0x6d40:0x18:0x586f33b st_earth:66:0x1c480:0x18c:0xd529f9bd st_emblem:64:0x5238:0x18:0xb8586f02 st_famicom:68:0x1b130:0x180:0xaf64ccc6 st_final:45:0x3040:0x18:0xed5319bb st_fzero:61:0x166b8:0x128:0x1431df4b st_greenhill:72:0x14bb0:0x14c:0x3e1d693e st_gw:63:0x23c30:0x15c:0xa5307e83 st_halberd:57:0x19a20:0x128:0xfa525001 st_heal:87:0x32e0:0x28:0xe48a9ba8 st_homerun:88:0xe518:0x28:0xe2bfb7d3 st_ice:62:0x1ba70:0x15c:0x726f2e67 st_jungle:51:0x27c38:0x19c:0x9c0afd26 st_kart:49:0x12630:0x140:0x8070c40b st_madein:65:0xaf70:0x28:0x4fd623ea st_mansion:47:0x137c0:0x140:0xabcc5f00 st_mariopast:48:0x133a0:0x70:0x394503a8 st_metalgear:71:0x18848:0x140:0x91ec1e49 st_newpork:69:0x6340:0x18:0xa5138fae st_norfair:54:0x19360:0x150:0x6abb807b st_oldin:53:0x72e8:0x18:0xb8a2fda9 st_orpheon:55:0x5198:0x18:0x4485525 st_otrain:86:0x2b10:0x28:0x5c1b2b3e st_palutena:67:0x13d48:0x48:0x8bcac7b2 st_pictchat:73:0x1b1d8:0x128:0x949382f5 st_pirates:52:0x1d690:0x128:0xeff685ed st_plankton:74:0x14090:0x54:0x5850a45f st_stadium:59:0x6ac8:0x18:0x4f64fb07 st_stageedit:85:0x80a0:0x18:0x170f84ad st_starfox:58:0x54b0:0x18:0x32c96c0d st_tbreak:89:0x5260:0x28:0x8196ad9a st_tengan:60:0xc890:0x18:0x64f08d40 st_village:70:0x168b8:0x58:0x5576c304";
}

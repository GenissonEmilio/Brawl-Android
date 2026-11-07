// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;
import java.util.concurrent.atomic.AtomicInteger;

public final class ImportLifecycleHostTest {
    private static void check(boolean ok, String message) {if(!ok) throw new AssertionError(message);}
    public static void main(String[] args) {
        AtomicInteger shown=new AtomicInteger();
        ImportLifecycle oldActivity=new ImportLifecycle();
        oldActivity.deliver(shown::incrementAndGet);
        check(shown.get()==0 && !oldActivity.canPresent(),"creation does not present background completion");
        oldActivity.resume();
        check(shown.get()==1 && oldActivity.canPresent(),"first resume delivers pending completion exactly once");
        oldActivity.resume();check(shown.get()==1,"resume cannot duplicate result");
        oldActivity.pause();oldActivity.deliver(shown::incrementAndGet);
        check(shown.get()==1 && !oldActivity.canPresent(),"backgrounding defers completion and polling");
        oldActivity.resume();check(shown.get()==2,"foreground receives deferred completion");
        oldActivity.pause();oldActivity.deliver(shown::incrementAndGet);oldActivity.destroy();
        oldActivity.resume();oldActivity.deliver(shown::incrementAndGet);
        check(shown.get()==2 && !oldActivity.canPresent(),"destroyed Activity cannot show or launch anything");
        ImportLifecycle recreated=new ImportLifecycle();recreated.resume();recreated.deliver(shown::incrementAndGet);
        check(shown.get()==3 && recreated.canPresent(),"recreated Activity independently presents current import state");
        System.out.println("PASS: import/recovery lifecycle creation, background, resume, destruction and recreation");
    }
}

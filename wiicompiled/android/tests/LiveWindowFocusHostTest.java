// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;
import java.io.BufferedReader;
import java.io.StringReader;

public final class LiveWindowFocusHostTest {
    private static int checks;
    private static void check(String text,boolean expected,String label) throws Exception {
        boolean actual=LiveWindowFocus.matches(new BufferedReader(new StringReader(text)),"com.genisson.brawl.android");
        ++checks;if(actual!=expected)throw new AssertionError(label);
    }
    public static void main(String[] args) throws Exception {
        String game="mCurrentFocus=Window{abcd u0 com.genisson.brawl.android/com.wither.nsmbw.GameActivity}\n";
        String app="mFocusedApp=ActivityRecord{abcd u0 com.genisson.brawl.android/com.wither.nsmbw.GameActivity t9}\n";
        check(game,true,"native game window accepted");
        check(app,true,"focused-app fallback accepted");
        check("mCurrentFocus=null\n"+app,true,"null current focus may use confirmed focused app");
        check("mCurrentFocus=Window{abcd u0 com.example.other/.Dialog}\n"+app,false,"unexpected focused window cannot be overridden by game app");
        check("mCurrentFocus=Window{abcd u0 com.genisson.brawl.android.validation/.Launcher}\n"+app,false,"QA package is not public game");
        check("mCurrentFocus=unrecognized\n"+app,false,"ambiguous current focus fails closed");
        check("mCurrentFocus=null\nmFocusedApp=null\n",false,"no focus fails closed");
        check("Window #1 Window{com.genisson.brawl.android/.Game}\n",false,"unrelated window records ignored");
        check(game+"mCurrentFocus=Window{efgh u0 com.example.other/.Dialog}\n",false,"conflicting display focus fails closed");
        check("mFocusedApp=ActivityRecord{abcd u0 com.example.other/.Activity t1}\n",false,"unexpected focused app fails closed");
        check("  "+game,true,"normal dump indentation accepted");
        System.out.println("PASS: live window focus parser checks="+checks);
    }
}

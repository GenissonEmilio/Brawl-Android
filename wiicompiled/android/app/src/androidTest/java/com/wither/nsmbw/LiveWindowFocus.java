// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

import java.io.BufferedReader;
import java.io.IOException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Reads only window-manager focus fields. Never returns or logs unrelated window contents. */
final class LiveWindowFocus {
    private static final Pattern COMPONENT=Pattern.compile("\\b([A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z0-9_]+)+)/[A-Za-z0-9_.$]+");
    static boolean matches(BufferedReader dump,String expectedPackage) throws IOException {
        boolean currentFound=false,currentExpected=true,appFound=false,appExpected=true;
        String line;
        while((line=dump.readLine())!=null) {
            String value=line.trim();
            if(value.startsWith("mCurrentFocus=")) {
                value=value.substring("mCurrentFocus=".length()).trim();
                if(value.equals("null")) continue;
                currentFound=true;
                Matcher component=COMPONENT.matcher(value);
                currentExpected&=value.startsWith("Window{")&&component.find()&&expectedPackage.equals(component.group(1));
            } else if(value.startsWith("mFocusedApp=")) {
                value=value.substring("mFocusedApp=".length()).trim();
                if(value.equals("null")) continue;
                appFound=true;
                Matcher component=COMPONENT.matcher(value);
                appExpected&=value.startsWith("ActivityRecord{")&&component.find()&&expectedPackage.equals(component.group(1));
            }
        }
        // An unexpected/ambiguous focused window may be a dialog over the game: never ignore it.
        return currentFound?currentExpected:appFound&&appExpected;
    }
}

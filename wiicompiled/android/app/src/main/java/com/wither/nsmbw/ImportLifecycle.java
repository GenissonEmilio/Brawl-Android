// SPDX-License-Identifier: GPL-3.0-only
package com.wither.nsmbw;

/** Main-thread gate: background import/recovery results must use a current resumed Activity. */
final class ImportLifecycle {
    private boolean resumed, destroyed;
    private Runnable pending;
    boolean canPresent() { return resumed && !destroyed; }
    void resume() {
        if (destroyed) return;
        resumed = true;
        Runnable result = pending;
        pending = null;
        if (result != null) result.run();
    }
    void pause() { resumed = false; }
    void destroy() { resumed = false; destroyed = true; pending = null; }
    void deliver(Runnable result) {
        if (destroyed) return;
        if (resumed) result.run();
        else pending = result;
    }
}

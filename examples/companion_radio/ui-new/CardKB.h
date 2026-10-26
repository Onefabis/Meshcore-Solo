#pragma once
// Included into UITask.cpp only: the CardKB (I2C keyboard) side of UITask.

#include <helpers/ui/I2CBusLock.h>

struct CardKBBusLock : I2CBusLock { };

#if defined(CARDKB_I2C)
// CardKB's "fn" column (key_map in M5Stack's unit_CardKB.cpp): Fn+<physical
// key> sends 0x80 + that key's row index, entirely disjoint from every other
// code this UI recognises. Indexed by (raw - 0x80); non-letter slots (digits,
// arrows, enter, tab, bs, space -- handled separately or unused) are 0.
static const char CARDKB_FN_BASE[48] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,                                     // esc,1-0,bs,tab
  'q','w','e','r','t','y','u','i','o','p', 0, 0,0,                // q-p, (unused), LEFT,UP
  'a','s','d','f','g','h','j','k','l', 0, 0,0,                    // a-l, enter, DOWN,RIGHT
  'z','x','c','v','b','n','m', 0,0,0,                             // z-m, comma,period,space
};
#endif

// Poll an optional CardKB (I2C keyboard, addr 0x5F) on CARDKB_I2C, feeding
// the same key queue as every physical button. Most of its output needs no
// translation at all: CardKB's own arrow/Enter/Esc byte codes are already
// identical to this UI's KEY_LEFT/UP/DOWN/RIGHT/ENTER/CANCEL (0xB4-0xB7, 13,
// 27), and Backspace (0x08) / printable ASCII (0x20-0x7E) collide with
// nothing that existed before. Plain Enter/arrows act like the physical
// centre button/joystick (grid commit/navigate) -- except in Compact mode's
// plain grid state (see below), which is designed to need no joystick at all.
// Tab (0x09, otherwise unused) is the Hold-Enter equivalent everywhere,
// including the ~30 non-keyboard Hold-Enter menus (message reply/navigate,
// Bot/Admin/Repeater, ...) and inside the on-screen keyboard itself (shift-
// lock, clear-all, accent popup on whatever cell is selected) -- it used to
// need a separate Fn+Tab for the latter, but that was pure redundancy: plain
// Tab already covered every case Fn+Tab did, just not while the keyboard was
// showing, so the carve-out was dropped instead of the shortcut. In Compact
// mode's plain grid state Tab means something more useful instead (opens the
// placeholder picker directly -- see below). Fn still gives two other clean,
// stateless modifiers:
//  - Fn+Enter (0xA3) submits the field (KEY_KB_ENTER) without needing to
//    navigate to the special row's DONE cell. The placeholder/accent popups
//    are modal and consume it first (dismiss them with Enter/Esc), same as
//    they consume every other key.
//  - Fn+<letter> opens the accent popup for that base letter directly
//    (KeyboardWidget::openAccentFor()) -- no arrow-hunting across the grid.
// CardKB is level-triggered (it keeps returning the held key's byte, not just
// once), so _cardkb_last_raw debounces it into one press per physical
// keypress, same as a MomentaryButton's CLICK event.
void UITask::toggleCardKBScript() {
  if (_kb.toggleExternalScript()) {
    char alert[32];
    snprintf(alert, sizeof(alert), "Language: %s", _kb.externalScriptLabel());
    showAlert(alert, 900);
  } else {
    showAlert("Set Additional language", 1200);
  }
}

void UITask::pollCardKB() {
#if defined(CARDKB_I2C)
  if (!_has_cardkb) {
    // A keyboard can be powered or reset after the ProMicro.  Keep trying to
    // attach in the background; a failed boot probe must not require another
    // firmware reset.
    const uint32_t now = millis();
    if ((int32_t)(now - _cardkb_next_probe) >= 0) {
      _cardkb_next_probe = now + 1000;
      if (probeCardKB()) {
        _has_cardkb = true;
        _cardkb_last_raw = 0;
        _cardkb_io_failures = 0;
        startCardKBCapture();
      }
    }
    return;
  }
  if (_cardkb_fn_pending && (uint32_t)(millis() - _cardkb_fn_pending_since) > CARDKB_FN_SEQUENCE_TIMEOUT_MS) {
    _cardkb_fn_pending = false;
    if (_display && !_display->isOn()) _display->turnOn();
    toggleLock();
    return;
  }
  // No artificial throttle: unlike a MomentaryButton (BUTTON_USE_INTERRUPTS
  // latches every edge in an ISR ring buffer, so it survives a blocking e-ink
  // refresh untouched), CardKB is plain I2C polling with no interrupt line on
  // the Grove cable and no onboard queue -- it only ever reports "what's held
  // right now". A press that starts and fully releases while curr->render()
  // is blocked is physically unobservable, no software fix can recover it.
  // Polling every loop() iteration (same as a digital button's check(), which
  // has no throttle either) just shrinks that miss window down to exactly the
  // render() duration instead of render()+30ms.
  // First, keys the background task captured while the display blocked, in order.
  while (_cardkb_q_tail != _cardkb_q_head) {
    uint8_t q = _cardkb_q[_cardkb_q_tail];
    _cardkb_q_tail = (uint8_t)((_cardkb_q_tail + 1) % CARDKB_Q_LEN);
    handleCardKBByte(q);
  }
  uint8_t raw;
  // The background sampler and the main loop share the primary Wire bus with
  // the RTC, sensors, and DRV2605.  Keep this foreground read under the same
  // mutex as the background task so a task switch cannot interleave I2C bytes.
  {
    CardKBBusLock bus_lock;
    if (!readCardKBEdge(raw)) return;
  }
  handleCardKBByte(raw);
#endif
}

#if defined(CARDKB_I2C)
bool UITask::probeCardKB() {
  CardKBBusLock bus_lock;
  CARDKB_I2C.beginTransmission(0x5F);
  return CARDKB_I2C.endTransmission() == 0;
}

bool UITask::readCardKBEdge(uint8_t& raw) {
  const uint8_t received = CARDKB_I2C.requestFrom(0x5F, 1);
  if (received != 1 || !CARDKB_I2C.available()) {
    // Discard a partial/error response so a stale byte cannot be interpreted
    // as the next key after a transient bus fault.
    while (CARDKB_I2C.available()) CARDKB_I2C.read();
    if (_cardkb_io_failures < 0xFF) _cardkb_io_failures++;
    if (_cardkb_io_failures >= 3) {
      _has_cardkb = false;
      _cardkb_next_probe = millis();
    }
    return false;
  }
  _cardkb_io_failures = 0;
  raw = CARDKB_I2C.read();
  if (raw == _cardkb_last_raw) return false;   // still held (or still released) -- no new edge
  _cardkb_last_raw = raw;
  return raw != 0;   // 0 = key just released, nothing to enqueue
}

// Single producer (the poll task) / single consumer (pollCardKB() in the
// loop) ring -- byte-sized indices, so no locking needed.
void UITask::cardkbQueuePush(uint8_t raw) {
  uint8_t next = (uint8_t)((_cardkb_q_head + 1) % CARDKB_Q_LEN);
  if (next == _cardkb_q_tail) return;   // full: drop (16 keys within one refresh)
  _cardkb_q[_cardkb_q_head] = raw;
  _cardkb_q_head = next;
}

#if defined(NRF52_PLATFORM)
void UITask::cardkbTask(void* self) {
  UITask* t = (UITask*)self;
  SemaphoreHandle_t m = (SemaphoreHandle_t)t->_cardkb_mutex;
  for (;;) {
    if (t->_cardkb_bg_active && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
      if (t->_cardkb_bg_active) {          // re-check: loop may have just finished
        uint8_t raw;
        if (t->readCardKBEdge(raw)) t->cardkbQueuePush(raw);
      }
      xSemaphoreGive(m);
    }
    vTaskDelay(ms2tick(10));   // a keypress lasts ~50+ ms, so 10 ms never misses one
  }
}
#endif
#endif

// Keep sampling the CardKB while the display blocks the loop, so keys typed
// during a (long, on big panels) refresh aren't lost. Called LAST in begin():
// the heap is tight on large-panel builds, and every screen is allocated with
// `new` -- a failed allocation leaves that screen silently unopenable. Creating
// the optional poll task afterwards means a short heap costs the task (the
// CardKB is then only read between frames, as without it), never a screen.
void UITask::startCardKBCapture() {
#if defined(CARDKB_I2C) && defined(NRF52_PLATFORM)
  if (!_has_cardkb) return;
  if (_cardkb_mutex) return;
  SemaphoreHandle_t m = xSemaphoreCreateMutex();
  if (!m) return;
  // Publish the mutex BEFORE creating the task: at higher priority it starts
  // running inside xTaskCreate() and reads _cardkb_mutex straight away.
  _cardkb_mutex = (void*)m;
  // TASK_PRIO_NORMAL (2) preempts the loop task (TASK_PRIO_LOW, 1); BLE runs higher.
  if (xTaskCreate(&UITask::cardkbTask, "cardkb", 512, this, TASK_PRIO_NORMAL, NULL) == pdPASS) {
    meshcore_i2c_bus_mutex = m;
    return;
  }
  _cardkb_mutex = nullptr;    // no task -> setCardKBBackground() stays a no-op
  meshcore_i2c_bus_mutex = nullptr;
  vSemaphoreDelete(m);
#endif
}

// Hand CardKB sampling to the background task (on = loop is about to block on
// the display) or take it back (off). Taking the mutex on the way out waits for
// any in-flight background I2C read to finish before the loop touches the bus.
void UITask::setCardKBBackground(bool on) {
#if defined(CARDKB_I2C) && defined(NRF52_PLATFORM)
  if (!_cardkb_mutex) return;
  if (on) {
    _cardkb_bg_active = true;
  } else {
    SemaphoreHandle_t m = (SemaphoreHandle_t)_cardkb_mutex;
    xSemaphoreTake(m, portMAX_DELAY);
    _cardkb_bg_active = false;
    xSemaphoreGive(m);
  }
#else
  (void)on;
#endif
}

void UITask::handleCardKBByte(uint8_t raw) {
#if defined(CARDKB_I2C)

  // Compact mode (Settings > Keyboard's "Ext. KB" row) hides the letter grid
  // entirely, and is meant to guarantee joystick-free operation: while it's
  // the active surface (KeyboardWidget::inPlainGridState() -- showing, no
  // popup open, not already mid cursor-move) arrows drive the text cursor
  // directly instead of a grid selection nobody could see anyway, and plain
  // Tab opens the placeholder picker directly instead of the row/col-dependent
  // Hold-Enter dispatch (which would be meaningless here -- row/col are never
  // deliberately navigated to in this mode). Cursor mode / the accent /
  // placeholder popups all render their own visible feedback regardless of
  // Compact, so none of this applies once inPlainGridState() is false --
  // arrows/Tab fall through to their normal meaning there (e.g. arrows drive
  // the placeholder/accent popup's own selection).
  bool compact_grid = _node_prefs && _node_prefs->keyboard_cardkb_compact && _kb.inPlainGridState();

  char key;
  if (raw == 0xA3) {          // Fn+Enter -- submit the field
    key = KEY_KB_ENTER;
  } else if (raw == 0x8B) {   // combined Fn+Backspace
    char woke = checkDisplayOn((char)raw);
    if (woke && !_locked && _kb.inPlainGridState()) toggleCardKBScript();
    return;
  } else if (raw == 0x80) {   // Fn state, or Fn+Esc on older firmware
    _cardkb_fn_pending = true;
    _cardkb_fn_pending_since = millis();
    return;
  } else if (_cardkb_fn_pending && raw == 0x08) { // sequential Fn, then Backspace
    _cardkb_fn_pending = false;
    char woke = checkDisplayOn((char)raw);
    if (woke && !_locked && _kb.inPlainGridState()) toggleCardKBScript();
    return;
  } else if (compact_grid && (raw == (uint8_t)KEY_LEFT || raw == (uint8_t)KEY_UP ||
                              raw == (uint8_t)KEY_DOWN || raw == (uint8_t)KEY_RIGHT)) {
    char woke = checkDisplayOn((char)raw);   // already sets _next_refresh=0 when the display was on
    if (woke && !_locked) _kb.moveCursorDirect((char)raw);
    return;
  } else if (raw == 0x09) {   // Tab -- Hold-Enter equivalent, always (single shortcut: there used to
    if (compact_grid) {       // also be a separate Fn+Tab for this, but plain Tab already covers every
      char woke = checkDisplayOn((char)raw);   // case Fn+Tab did -- outside the keyboard, and now inside it
      if (woke && !_locked) _kb.openPlaceholders();   // too -- so the modifier was pure redundancy)
      return;
    }
    key = KEY_CONTEXT_MENU;
  } else if (raw >= 0x80 && raw <= 0xAF) {   // Fn+<letter> -- open its accent popup
    char base = CARDKB_FN_BASE[raw - 0x80];
    if (base == 0) return;   // Fn+digit/symbol/arrow -- not used by this UI
    char woke = checkDisplayOn(base);
    // Every other key here goes through enqueueKey(), so it's naturally eaten
    // while locked (see the dequeue-time "if (!_locked && curr)" gate in
    // loop()). This path calls into the keyboard widget directly instead, so
    // it needs its own _locked check -- otherwise a stray Fn+letter (e.g. the
    // keyboard was left open before the device locked, or brushed against in
    // a pocket) could pop the accent popup while the screen is supposed to
    // ignore all input.
    if (woke && !_locked) _kb.openAccentFor(base);
    return;
  } else {
    // Plain Enter would otherwise commit whatever grid cell row/col happen to
    // be frozen at (there's no grid navigation to have deliberately landed on
    // one in Compact) -- submit instead, same as Fn+Enter. Backspace/ASCII
    // passthrough is unaffected by Compact either way.
    key = (compact_grid && raw == (uint8_t)KEY_ENTER) ? KEY_KB_ENTER : (char)raw;
  }
  enqueueKey(checkDisplayOn(key));
#endif
}

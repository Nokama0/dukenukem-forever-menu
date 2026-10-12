// Level stats glue (levelstats.h holds the rules): one outlined line top left from progress.h's last read, which
// frame.cpp refreshes once a second while the switch is on in a level. Game thread; reads no engine memory itself.
#include "levelstats.h"
#include "progress.h"
#include "canvas.h"
#include "esp_layout.h"   // esp_draw_label: the ESP's outlined text
#include "font.h"
#include "config.h"
#include "input.h"
#include "ui/menu.h"      // menu_closing_animation_active
#include "log.h"

static unsigned s_calls, s_drawn, s_hidPanel, s_noChapter;

// inLevel is world_frame's: 0 while paused (the pause menu and the game's menus over a level), as the ESP is gated.
void levelstats_draw(int W, int H, int inLevel) {
  // A DLC level counts its chapter in the DLC table with the DLC's flags (progress.cpp's DLC mode).
  const int dlc = progress_is_dlc();
  const int chapter = dlc ? progress_dlc_chapter_now() : progress_chapter_now();
  const int panel = input_panel_open() || menu_closing_animation_active();
  if (g_cfg.levelStats && inLevel) {
    if (!ls_visible(1, 1, chapter, panel)) {
      if (panel) s_hidPanel++; else s_noChapter++;
    } else {
      int n = 0;
      const PgChapter* rows = dlc ? progress_dlc_chapters(&n) : progress_chapters(&n);
      if (chapter < n) {
        char line[LS_LINE_CAP];
        ls_ego_line(line, sizeof line, rows[chapter].egoFound, rows[chapter].egoTotal);
        // Teal, the Progress tab's colour; green once the chapter's awards are all found.
        const bool all = rows[chapter].egoTotal > 0 && rows[chapter].egoFound >= rows[chapter].egoTotal;
        canvas_open(W, H);
        esp_draw_label(LS_MARGIN + ESP_LABEL_OUTLINE_R, LS_MARGIN + ESP_LABEL_OUTLINE_R, line,
                       all ? C_GREEN : C_PROGRESS, FONT_UI);
        s_drawn++;
      }
    }
  }
  if (++s_calls % 600 != 0) return;   // like the other instruments: every 600 frames
  if (s_drawn || s_hidPanel || s_noChapter)
    np_log("levelstats: drawn %u, hidden: panel %u, map in no chapter %u (chapter now %d)", s_drawn, s_hidPanel,
           s_noChapter, chapter);
  s_drawn = s_hidPanel = s_noChapter = 0;
}

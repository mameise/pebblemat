// Bally 493 - Pebble Time 2 (Emery) Slot Machine
// Vollstaendiger Slot mit beiden Risikoleitern, Quer-Spiel und Pfeilen.
//
// Buttons:
//   UP    = A  (Start / 1:1 im Risiko)
//   SELECT= B  (Auszahlen idle / Stopp im Risiko / Spin-Stop im Spin /
//               Quer-Stop in der Lichtorgel)
//   DOWN  = C  (Muenze einwerfen idle / Teilen im Risiko)
//   DOWN lang = Quer in Mitte (an markierten Stufen)
//   BACK  = App verlassen (Stand wird automatisch persistiert)

#include <pebble.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Display-Geometrie (Emery: 200x228)
// ============================================================
#define SCR_W 200
#define SCR_H 228

#define HEADER_H 18
#define FOOTER_H 24
#define LEITER_W 32
#define WALZEN_X (LEITER_W)
#define WALZEN_W (SCR_W - 2 * LEITER_W)
#define BODY_Y0 HEADER_H
#define BODY_Y1 (SCR_H - FOOTER_H)
#define BODY_H (BODY_Y1 - BODY_Y0)

// ============================================================
// Spiel-Konstanten
// ============================================================
#define EINSATZ 20  // Cent

// Symbol-IDs (Walzen). Pixel-Art-Symbole als IDs, Render via switch.
typedef enum {
  SYM_NONE = 0,
  SYM_10C, SYM_20C, SYM_30C, SYM_40C, SYM_60C, SYM_80C, SYM_120C, SYM_170C,
  SYM_K_ROT, SYM_K_GELB,    // Kronen
  SYM_4, SYM_9, SYM_3,      // 4-9-3 Spezial
  SYM_KARIERT,
  SYM_BLANK,
} SymbolID;

// Stufen-Typen
typedef enum {
  ST_NULL = 0,
  ST_AUS,
  ST_CENT,
  ST_SP,
  ST_MULTI,
  ST_GIGA,
  ST_MA,
  ST_MYSTERY,
} StufenTyp;

typedef struct {
  StufenTyp typ;
  uint16_t  val;
} Stufe;

// Linke Leiter (oben -> unten), idx 0 = top
static const Stufe LEITER_LINKS[] = {
  {ST_GIGA, 9},
  {ST_GIGA, 4},
  {ST_GIGA, 2},
  {ST_SP, 12},     // Quer-Trigger (Set B)
  {ST_SP, 6},      // Quer-Trigger (Set A)
  {ST_SP, 3},
  {ST_AUS, 0},
  {ST_CENT, 170},
  {ST_CENT, 80},
  {ST_CENT, 40},
  {ST_CENT, 20},
  {ST_NULL, 0},
};
#define LEITER_LINKS_N (sizeof(LEITER_LINKS)/sizeof(LEITER_LINKS[0]))

static const Stufe LEITER_RECHTS[] = {
  {ST_MULTI, 100},
  {ST_MULTI, 50},
  {ST_MULTI, 24},
  {ST_MULTI, 12},  // Quer-Trigger (Set C)
  {ST_MA, 0},
  {ST_SP, 10},     // Quer-Trigger (Set B)
  {ST_SP, 5},      // Quer-Trigger (Set A)
  {ST_SP, 2},
  {ST_AUS, 0},
  {ST_CENT, 120},
  {ST_CENT, 60},
  {ST_CENT, 30},
  {ST_CENT, 10},
  {ST_NULL, 0},
};
#define LEITER_RECHTS_N (sizeof(LEITER_RECHTS)/sizeof(LEITER_RECHTS[0]))

// Mittel-Quer-Spiele
static const Stufe QUER_SET_A[] = {
  {ST_SP, 12}, {ST_SP, 6}, {ST_MULTI, 12}, {ST_SP, 3}, {ST_SP, 10}
};
#define QUER_SET_A_N (sizeof(QUER_SET_A)/sizeof(QUER_SET_A[0]))

static const Stufe QUER_SET_B[] = {
  {ST_SP, 12}, {ST_SP, 6}, {ST_MULTI, 50}
};
#define QUER_SET_B_N (sizeof(QUER_SET_B)/sizeof(QUER_SET_B[0]))

static const Stufe QUER_SET_C[] = {
  {ST_MULTI, 50}, {ST_MULTI, 12}, {ST_MYSTERY, 0}
};
#define QUER_SET_C_N (sizeof(QUER_SET_C)/sizeof(QUER_SET_C[0]))

// Quer-Trigger: an welcher (Seite, idx) wird welches Set angeboten
// Seite 'L'=0, 'R'=1
typedef struct { uint8_t seite; uint8_t idx; char set; } QuerTrigger;
static const QuerTrigger QUER_TRIGGERS[] = {
  {0, 3, 'B'},  // SP 12 links
  {0, 4, 'A'},  // SP 6 links
  {1, 3, 'C'},  // MULTI 12 rechts
  {1, 5, 'B'},  // SP 10 rechts
  {1, 6, 'A'},  // SP 5 rechts
};
#define QUER_TRIGGERS_N (sizeof(QUER_TRIGGERS)/sizeof(QUER_TRIGGERS[0]))

// Mystery-Aufloesungen mit Gewichten
typedef struct { Stufe s; uint8_t weight; } MysteryOpt;
static const MysteryOpt MYSTERY_OPTS[] = {
  {{ST_GIGA, 3}, 6},
  {{ST_GIGA, 4}, 3},
  {{ST_GIGA, 9}, 1},
};
#define MYSTERY_OPTS_N (sizeof(MYSTERY_OPTS)/sizeof(MYSTERY_OPTS[0]))

// Walzen-Streifen: Verteilung der Symbole. Walze 0 (links), 1 (mitte),
// 2 (rechts). Mitte hat 1 sichtbares Symbol, links/rechts 2.
// Wir bauen "Strips" - Sequenzen aus denen wir zufaellig samplen.
// Gewichtung an Originalmechanik angelehnt (haeufige Cent, seltene Kronen).
static const SymbolID STRIP_0[] = {
  SYM_10C, SYM_BLANK, SYM_40C, SYM_BLANK, SYM_K_ROT, SYM_20C, SYM_4,
  SYM_80C, SYM_BLANK, SYM_40C, SYM_3, SYM_170C, SYM_BLANK,
  SYM_20C, SYM_K_GELB, SYM_KARIERT, SYM_40C, SYM_BLANK, SYM_120C,
  SYM_9, SYM_BLANK, SYM_80C, SYM_K_ROT, SYM_30C, SYM_60C
};
#define STRIP_0_N (sizeof(STRIP_0)/sizeof(STRIP_0[0]))

static const SymbolID STRIP_1[] = {
  SYM_K_ROT, SYM_BLANK, SYM_20C, SYM_K_GELB, SYM_BLANK, SYM_40C, SYM_9,
  SYM_KARIERT, SYM_K_ROT, SYM_BLANK, SYM_30C, SYM_K_GELB, SYM_BLANK,
  SYM_60C, SYM_K_ROT, SYM_BLANK, SYM_K_GELB, SYM_20C, SYM_BLANK
};
#define STRIP_1_N (sizeof(STRIP_1)/sizeof(STRIP_1[0]))

static const SymbolID STRIP_2[] = {
  SYM_10C, SYM_30C, SYM_BLANK, SYM_60C, SYM_K_GELB, SYM_3, SYM_120C,
  SYM_BLANK, SYM_10C, SYM_K_ROT, SYM_30C, SYM_BLANK, SYM_60C, SYM_4,
  SYM_KARIERT, SYM_120C, SYM_BLANK, SYM_30C, SYM_K_GELB, SYM_10C
};
#define STRIP_2_N (sizeof(STRIP_2)/sizeof(STRIP_2[0]))

// ============================================================
// Spiel-State (alles was wichtig ist)
// ============================================================
typedef enum {
  STATE_IDLE = 0,
  STATE_SPINNING,
  STATE_RISIKO,
} GameState;

// Pfeile: bis zu 64 Stufen pro Seite => 64-bit Bitmasken reichen locker
typedef struct {
  // Reels: links hat 2 Symbole, mitte 1, rechts 2
  SymbolID reel_l[2];
  SymbolID reel_m[1];
  SymbolID reel_r[2];
  // Spin-Animation: ein virtueller "Strip-Offset" pro Walze und Zielzeit
  int      spin_offset[3];
  uint32_t spin_stop_at[3]; // ms Tick, 0 = bereits gestoppt
  bool     spin_stop_req[3];
  uint32_t spin_start_ms;

  GameState state;
  char message[32];
  uint32_t message_until;

  // Risiko
  int8_t risiko_left_idx;   // -1 wenn nicht aktiv
  int8_t risiko_right_idx;
  char   risiko_seite;       // 'L' oder 'R' oder 0
  bool   risiko_can_teilen;

  // Bonuspfeile-Bitmasken pro Stufe
  uint64_t pfeile_links;
  uint64_t pfeile_rechts;

  // Quer-Lichtorgel
  bool  quer_active;
  char  quer_set_name;       // 'A' 'B' 'C'
  uint8_t quer_pos;
  uint32_t quer_last_step_ms;

  // Speicher
  int32_t muenzspeicher;     // Cent
  int32_t sonderspiele;
  int32_t multispiele;
  int32_t gigaspiele;
  char    serie_mode;        // 'n' (normal), 's', 'm', 'g'

  // Statistik
  int32_t stats_spiele;
  int32_t stats_gewinne;

  // UI
  uint32_t flash_last_ms;
  bool     flash;

  // Long-Press DOWN tracking (fuer Quer)
  uint32_t down_pressed_ms;  // 0 wenn nicht gehalten
  bool     down_long_fired;
} Slot;

static Slot g;
static Window *s_window;
static Layer  *s_main_layer;
static AppTimer *s_tick_timer = NULL;

// Color helpers (Emery hat 64-Farben, GColor8)
#define COL_BG       GColorDarkGray
#define COL_BORDER   GColorYellow
#define COL_LEITER_L_BG GColorOxfordBlue
#define COL_LEITER_R_BG GColorOxfordBlue
#define COL_KRONE_BG GColorOrange
#define COL_TEXT     GColorWhite
#define COL_DIM      GColorLightGray
#define COL_RED      GColorRed
#define COL_GREEN    GColorIslamicGreen
#define COL_GOLD     GColorYellow
#define COL_HIGHLIGHT GColorRichBrilliantLavender

// ============================================================
// Forward declarations
// ============================================================
static void redraw(void);
static void schedule_tick(void);
static uint32_t now_ms(void);
static void state_save(void);
static void state_load(void);
static void start_spiel(void);
static void evaluate_spin(void);
static void risiko_starten(int8_t left_idx, int8_t right_idx, int line_idx, uint16_t cent);
static void risiko_1zu1(void);
static void risiko_stopp(void);
static void risiko_teilen(void);
static void quer_start_action(void);
static void quer_stop_action(void);
static void gutschreiben(StufenTyp t, uint16_t v);
static bool teilbar(StufenTyp t, uint16_t v, int8_t idx, char seite);
static void bonuspfeil_check(char seite, int8_t neuer_idx);
static void risiko_verloren(void);
static void reset_risiko_state(void);
static const Stufe* current_leiter(int *n_out);
static int8_t current_idx(void);

// ============================================================
// Util
// ============================================================
static uint32_t now_ms(void) {
  // Pebble hat keinen Wall-Clock-ms, aber app_timer_register und time_ms()
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (uint32_t)((s & 0xFFFFFF) * 1000U) + ms;
}

static bool rng_chance(int percent) {
  return (rand() % 100) < percent;
}

static void message_set(const char *s, uint32_t ms_lifetime) {
  strncpy(g.message, s, sizeof(g.message) - 1);
  g.message[sizeof(g.message) - 1] = 0;
  g.message_until = now_ms() + ms_lifetime;
}

// ============================================================
// Persistence
// ============================================================
#define PKEY_BLOB 1
#define PKEY_VERSION 2
#define SAVE_VERSION 1

typedef struct {
  uint32_t version;
  int32_t muenzspeicher;
  int32_t sonderspiele;
  int32_t multispiele;
  int32_t gigaspiele;
  uint64_t pfeile_links;
  uint64_t pfeile_rechts;
  int32_t stats_spiele;
  int32_t stats_gewinne;
} SaveBlob;

static void state_save(void) {
  SaveBlob b = {0};
  b.version = SAVE_VERSION;
  b.muenzspeicher = g.muenzspeicher;
  b.sonderspiele = g.sonderspiele;
  b.multispiele = g.multispiele;
  b.gigaspiele = g.gigaspiele;
  b.pfeile_links = g.pfeile_links;
  b.pfeile_rechts = g.pfeile_rechts;
  b.stats_spiele = g.stats_spiele;
  b.stats_gewinne = g.stats_gewinne;
  persist_write_data(PKEY_BLOB, &b, sizeof(b));
}

static void state_load(void) {
  if (!persist_exists(PKEY_BLOB)) return;
  SaveBlob b = {0};
  int n = persist_read_data(PKEY_BLOB, &b, sizeof(b));
  if (n != sizeof(b) || b.version != SAVE_VERSION) return;
  g.muenzspeicher = b.muenzspeicher;
  g.sonderspiele = b.sonderspiele;
  g.multispiele = b.multispiele;
  g.gigaspiele = b.gigaspiele;
  g.pfeile_links = b.pfeile_links;
  g.pfeile_rechts = b.pfeile_rechts;
  g.stats_spiele = b.stats_spiele;
  g.stats_gewinne = b.stats_gewinne;
}

// ============================================================
// Initial-State
// ============================================================
static void state_init(void) {
  memset(&g, 0, sizeof(g));
  g.state = STATE_IDLE;
  g.risiko_left_idx = -1;
  g.risiko_right_idx = -1;
  g.serie_mode = 'n';
  strcpy(g.message, "A=START");
  // Reels mit Default-Symbolen
  g.reel_l[0] = SYM_40C; g.reel_l[1] = SYM_20C;
  g.reel_m[0] = SYM_K_ROT;
  g.reel_r[0] = SYM_30C; g.reel_r[1] = SYM_10C;
  state_load();
}

// ============================================================
// Spiel-Logik
// ============================================================
static bool can_start(void) {
  if (g.gigaspiele > 0 || g.multispiele > 0 || g.sonderspiele > 0) return true;
  return g.muenzspeicher >= EINSATZ;
}

static void start_spiel(void) {
  if (g.state != STATE_IDLE) return;
  if (!can_start()) {
    message_set("MUENZE FEHLT", 1200);
    return;
  }
  // Modus bestimmen
  if (g.gigaspiele > 0) { g.gigaspiele--; g.serie_mode = 'g'; }
  else if (g.multispiele > 0) { g.multispiele--; g.serie_mode = 'm'; }
  else if (g.sonderspiele > 0) { g.sonderspiele--; g.serie_mode = 's'; }
  else { g.muenzspeicher -= EINSATZ; g.serie_mode = 'n'; }

  reset_risiko_state();
  // Walzen-Spin starten
  g.state = STATE_SPINNING;
  uint32_t t = now_ms();
  g.spin_start_ms = t;
  for (int i = 0; i < 3; i++) {
    g.spin_stop_at[i] = t + 900 + i * 350;  // gestaffelt 900/1250/1600 ms
    g.spin_stop_req[i] = false;
    g.spin_offset[i] = rand();
  }
  g.stats_spiele++;
  message_set("SPIN", 800);
}

static void request_stop_naechste(void) {
  // SELECT in Spinning: nächste noch laufende Walze schneller stoppen
  uint32_t t = now_ms();
  for (int i = 0; i < 3; i++) {
    if (g.spin_stop_at[i] > t && !g.spin_stop_req[i]) {
      g.spin_stop_req[i] = true;
      g.spin_stop_at[i] = t + 80;
      return;
    }
  }
}

static SymbolID reel_at(int reel, int row) {
  // Berechnet was angezeigt wird basierend auf spin_offset und row
  const SymbolID *strip;
  int n;
  switch (reel) {
    case 0: strip = STRIP_0; n = STRIP_0_N; break;
    case 1: strip = STRIP_1; n = STRIP_1_N; break;
    default: strip = STRIP_2; n = STRIP_2_N; break;
  }
  int off = g.spin_offset[reel] + row;
  return strip[((off % n) + n) % n];
}

static void apply_final_reels(void) {
  g.reel_l[0] = reel_at(0, 0); g.reel_l[1] = reel_at(0, 1);
  g.reel_m[0] = reel_at(1, 0);
  g.reel_r[0] = reel_at(2, 0); g.reel_r[1] = reel_at(2, 1);
}

// Bewertet die zwei Gewinn-Linien
// Linie A oben: (reel_l[0], reel_m[0], reel_r[0])
// Linie B unten: (reel_l[1], reel_m[0], reel_r[1])
typedef struct { uint16_t cent; bool spezial; int line; } LineWin;

static int sym_cent(SymbolID s) {
  switch (s) {
    case SYM_10C: return 10;
    case SYM_20C: return 20;
    case SYM_30C: return 30;
    case SYM_40C: return 40;
    case SYM_60C: return 60;
    case SYM_80C: return 80;
    case SYM_120C: return 120;
    case SYM_170C: return 170;
    default: return 0;
  }
}

static LineWin eval_line(SymbolID a, SymbolID m, SymbolID b, int line_idx) {
  LineWin w = {0, false, line_idx};
  // 4-9-3 Spezial-Kombo
  if (a == SYM_4 && m == SYM_9 && b == SYM_3) {
    w.cent = 100; w.spezial = true; return w;
  }
  // 120-20-80
  if (a == SYM_120C && m == SYM_20C && b == SYM_80C) {
    w.cent = 20; w.spezial = true; return w;
  }
  // Krone in Mitte
  if (m == SYM_K_ROT) { w.cent = 10; return w; }
  if (m == SYM_K_GELB) { w.cent = 20; return w; }
  // 3 gleiche Cent
  int ca = sym_cent(a), cb = sym_cent(b), cm = sym_cent(m);
  if (ca > 0 && ca == cm && cm == cb) { w.cent = ca; return w; }
  return w;
}

static void evaluate_spin(void) {
  apply_final_reels();
  LineWin a = eval_line(g.reel_l[0], g.reel_m[0], g.reel_r[0], 0);
  LineWin b = eval_line(g.reel_l[1], g.reel_m[0], g.reel_r[1], 1);
  LineWin best = (a.cent >= b.cent) ? a : b;
  if (best.cent == 0) {
    g.state = STATE_IDLE;
    message_set("KEIN GEWINN", 1500);
    return;
  }
  // Risiko anbieten: passende Stufen in den Leitern finden
  int8_t li = -1, ri = -1;
  for (uint8_t i = 0; i < LEITER_LINKS_N; i++) {
    if (LEITER_LINKS[i].typ == ST_CENT && LEITER_LINKS[i].val == best.cent) {
      li = i; break;
    }
  }
  for (uint8_t i = 0; i < LEITER_RECHTS_N; i++) {
    if (LEITER_RECHTS[i].typ == ST_CENT && LEITER_RECHTS[i].val == best.cent) {
      ri = i; break;
    }
  }
  if (li < 0 && ri < 0) {
    // Sondergewinn (z.B. 100c von 4-9-3) -> direkt cash
    g.muenzspeicher += best.cent;
    g.state = STATE_IDLE;
    char buf[24];
    snprintf(buf, sizeof(buf), "+%d c", best.cent);
    message_set(buf, 1500);
    return;
  }
  risiko_starten(li, ri, best.line, best.cent);
}

static void risiko_starten(int8_t left_idx, int8_t right_idx, int line_idx, uint16_t cent) {
  g.risiko_left_idx = left_idx;
  g.risiko_right_idx = right_idx;
  g.risiko_seite = (left_idx >= 0) ? 'L' : 'R';

  // Pfeil-Check beim Einstieg
  int8_t idx = (g.risiko_seite == 'L') ? left_idx : right_idx;
  bonuspfeil_check(g.risiko_seite, idx);

  // Wenn durch Pfeil auf AUS gelandet: Trostpreis (keep going)
  int n; const Stufe *l = current_leiter(&n);
  int8_t cur = current_idx();
  if (cur >= 0 && l[cur].typ == ST_AUS) {
    // Trostpreis: kleiner SP-Bonus
    g.sonderspiele += 3;
    int8_t nidx = (cur > 0) ? cur - 1 : 0;
    if (g.risiko_seite == 'L') g.risiko_left_idx = nidx;
    else g.risiko_right_idx = nidx;
    bonuspfeil_check(g.risiko_seite, nidx);
    cur = current_idx();
  }
  if (cur < 0 || l[cur].typ == ST_NULL) {
    g.muenzspeicher += cent;
    reset_risiko_state();
    g.state = STATE_IDLE;
    char buf[24];
    snprintf(buf, sizeof(buf), "+%d c", cent);
    message_set(buf, 1500);
    return;
  }

  g.state = STATE_RISIKO;
  Stufe st = l[cur];
  g.risiko_can_teilen = teilbar(st.typ, st.val, cur, g.risiko_seite);
  char buf[24];
  if (st.typ == ST_CENT) {
    snprintf(buf, sizeof(buf), "L%c: %dc", (line_idx == 0 ? 'A' : 'B'), st.val);
  } else if (st.typ == ST_SP) {
    snprintf(buf, sizeof(buf), "SP %d?", st.val);
  } else if (st.typ == ST_MULTI) {
    snprintf(buf, sizeof(buf), "M %d?", st.val);
  } else if (st.typ == ST_GIGA) {
    snprintf(buf, sizeof(buf), "G %d?", st.val);
  } else if (st.typ == ST_MA) {
    strcpy(buf, "MA?");
  } else {
    strcpy(buf, "?");
  }
  message_set(buf, 60000);
}

static const Stufe* current_leiter(int *n_out) {
  if (g.risiko_seite == 'L') {
    if (n_out) *n_out = LEITER_LINKS_N;
    return LEITER_LINKS;
  }
  if (n_out) *n_out = LEITER_RECHTS_N;
  return LEITER_RECHTS;
}

static int8_t current_idx(void) {
  if (g.risiko_seite == 'L') return g.risiko_left_idx;
  if (g.risiko_seite == 'R') return g.risiko_right_idx;
  return -1;
}

static void set_current_idx(int8_t v) {
  if (g.risiko_seite == 'L') g.risiko_left_idx = v;
  else if (g.risiko_seite == 'R') g.risiko_right_idx = v;
}

static bool teilbar(StufenTyp t, uint16_t v, int8_t idx, char seite) {
  if (t == ST_AUS || t == ST_NULL) return false;
  if (t == ST_CENT && v <= 20) return false;
  // Mindestens eine echte Folge-Stufe darunter?
  int n;
  const Stufe *l = (seite == 'L') ? LEITER_LINKS : LEITER_RECHTS;
  n = (seite == 'L') ? LEITER_LINKS_N : LEITER_RECHTS_N;
  for (int j = idx + 1; j < n; j++) {
    if (l[j].typ != ST_AUS && l[j].typ != ST_NULL) return true;
  }
  return false;
}

static void bonuspfeil_check(char seite, int8_t neuer_idx) {
  uint64_t *pfeile = (seite == 'L') ? &g.pfeile_links : &g.pfeile_rechts;
  int8_t cur = neuer_idx;
  while (cur >= 0 && (*pfeile & (1ULL << cur))) {
    *pfeile &= ~(1ULL << cur);
    if (cur == 0) break;
    cur--;
  }
  if (seite == 'L') g.risiko_left_idx = cur;
  else g.risiko_right_idx = cur;
}

static void reset_risiko_state(void) {
  g.risiko_left_idx = -1;
  g.risiko_right_idx = -1;
  g.risiko_seite = 0;
  g.risiko_can_teilen = false;
  g.quer_active = false;
  g.quer_set_name = 0;
}

static void risiko_verloren(void) {
  int8_t cur = current_idx();
  if (g.risiko_seite == 'L' && cur >= 0) g.pfeile_links |= (1ULL << cur);
  else if (g.risiko_seite == 'R' && cur >= 0) g.pfeile_rechts |= (1ULL << cur);
  message_set("VERLOREN", 1500);
  reset_risiko_state();
  g.state = STATE_IDLE;
  vibes_short_pulse();
}

static void gutschreiben(StufenTyp t, uint16_t v) {
  switch (t) {
    case ST_CENT:
      g.muenzspeicher += v;
      break;
    case ST_SP:
      g.sonderspiele += v;
      break;
    case ST_MULTI:
      // Sonderspiele werden in den naechsten Multi gerollt (analog Pi)
      if (g.sonderspiele > 0) { v = v + g.sonderspiele; g.sonderspiele = 0; }
      g.multispiele += v;
      if (v >= 100) g.stats_gewinne++;
      break;
    case ST_GIGA:
      g.gigaspiele += v;
      break;
    case ST_MA: {
      // Multi-Aus: zufaelliger Multi-Wert
      int r = rand() % 10;
      int m = (r < 6) ? 12 : (r < 9 ? 24 : 50);
      if (g.sonderspiele > 0) { m += g.sonderspiele; g.sonderspiele = 0; }
      g.multispiele += m;
      break;
    }
    case ST_MYSTERY: {
      // Gewichtet aufloesen
      int total = 0;
      for (int i = 0; i < (int)MYSTERY_OPTS_N; i++) total += MYSTERY_OPTS[i].weight;
      int r = rand() % total;
      for (int i = 0; i < (int)MYSTERY_OPTS_N; i++) {
        if (r < MYSTERY_OPTS[i].weight) { gutschreiben(MYSTERY_OPTS[i].s.typ, MYSTERY_OPTS[i].s.val); return; }
        r -= MYSTERY_OPTS[i].weight;
      }
      break;
    }
    default: break;
  }
}

static void risiko_1zu1(void) {
  if (g.state != STATE_RISIKO || g.quer_active) return;
  int8_t cur = current_idx();
  if (cur < 0) return;
  if (rng_chance(50)) {
    // verloren
    risiko_verloren();
    return;
  }
  // gewonnen, eins hoch
  int8_t new_idx = (cur > 0) ? cur - 1 : 0;
  set_current_idx(new_idx);
  bonuspfeil_check(g.risiko_seite, new_idx);

  int n; const Stufe *l = current_leiter(&n);
  int8_t ci = current_idx();
  Stufe st = l[ci];

  // AUS-Sonderfall: Trostpreis + eins weiter
  if (st.typ == ST_AUS) {
    g.sonderspiele += 3;
    int8_t nx = (ci > 0) ? ci - 1 : 0;
    set_current_idx(nx);
    bonuspfeil_check(g.risiko_seite, nx);
    ci = current_idx();
    st = l[ci];
  }
  if (st.typ == ST_NULL) {
    reset_risiko_state();
    g.state = STATE_IDLE;
    message_set("ENDE", 1200);
    return;
  }
  g.risiko_can_teilen = teilbar(st.typ, st.val, ci, g.risiko_seite);
  char buf[24];
  switch (st.typ) {
    case ST_CENT:  snprintf(buf, sizeof(buf), "+%dc?", st.val); break;
    case ST_SP:    snprintf(buf, sizeof(buf), "SP %d?", st.val); break;
    case ST_MULTI: snprintf(buf, sizeof(buf), "M %d?", st.val); break;
    case ST_GIGA:  snprintf(buf, sizeof(buf), "G %d?", st.val); break;
    case ST_MA:    strcpy(buf, "MA?"); break;
    default:       strcpy(buf, "?"); break;
  }
  message_set(buf, 60000);
}

static void risiko_stopp(void) {
  if (g.state != STATE_RISIKO || g.quer_active) return;
  int n; const Stufe *l = current_leiter(&n);
  int8_t cur = current_idx();
  if (cur < 0) return;
  Stufe st = l[cur];
  gutschreiben(st.typ, st.val);
  char buf[24];
  switch (st.typ) {
    case ST_CENT:  snprintf(buf, sizeof(buf), "+%dc", st.val); break;
    case ST_SP:    snprintf(buf, sizeof(buf), "+%d SP", st.val); break;
    case ST_MULTI: snprintf(buf, sizeof(buf), "+%d M", st.val); break;
    case ST_GIGA:  snprintf(buf, sizeof(buf), "+%d G", st.val); break;
    case ST_MA:    strcpy(buf, "+MA"); break;
    default:       strcpy(buf, "+0"); break;
  }
  message_set(buf, 1800);
  if (st.typ == ST_GIGA || (st.typ == ST_MULTI && st.val >= 100)) {
    vibes_double_pulse();
  } else {
    vibes_short_pulse();
  }
  reset_risiko_state();
  g.state = STATE_IDLE;
}

static void risiko_teilen(void) {
  if (g.state != STATE_RISIKO || g.quer_active) return;
  if (!g.risiko_can_teilen) return;
  int n; const Stufe *l = current_leiter(&n);
  int8_t cur = current_idx();
  if (cur < 0) return;
  Stufe st = l[cur];
  if (st.typ == ST_AUS || st.typ == ST_NULL) return;
  if (st.typ == ST_CENT && st.val <= 20) return;
  uint16_t half = st.val / 2;
  int8_t new_idx = cur + 1;
  while (new_idx < n && (l[new_idx].typ == ST_AUS || l[new_idx].typ == ST_NULL)) {
    new_idx++;
  }
  if (new_idx >= n) {
    // nichts darunter, einfach gutschreiben
    if (half > 0) gutschreiben(st.typ, half);
    reset_risiko_state();
    g.state = STATE_IDLE;
    message_set("TEIL+ENDE", 1500);
    return;
  }
  if (half > 0) gutschreiben(st.typ, half);
  set_current_idx(new_idx);
  Stufe nx = l[new_idx];
  g.risiko_can_teilen = teilbar(nx.typ, nx.val, new_idx, g.risiko_seite);
  char buf[24];
  snprintf(buf, sizeof(buf), "TEIL +%d", half);
  message_set(buf, 1500);
}

static char quer_set_for_current(void) {
  int8_t cur = current_idx();
  uint8_t seite = (g.risiko_seite == 'L') ? 0 : 1;
  for (uint8_t i = 0; i < QUER_TRIGGERS_N; i++) {
    if (QUER_TRIGGERS[i].seite == seite && QUER_TRIGGERS[i].idx == cur) {
      return QUER_TRIGGERS[i].set;
    }
  }
  return 0;
}

static void quer_start_action(void) {
  if (g.state != STATE_RISIKO || g.quer_active) return;
  char set = quer_set_for_current();
  if (set == 0) return;
  g.quer_active = true;
  g.quer_set_name = set;
  g.quer_pos = 0;
  g.quer_last_step_ms = now_ms();
  message_set("QUER!", 1000);
}

static const Stufe* quer_set_items(char set_name, int *n_out) {
  switch (set_name) {
    case 'A': *n_out = QUER_SET_A_N; return QUER_SET_A;
    case 'B': *n_out = QUER_SET_B_N; return QUER_SET_B;
    case 'C': *n_out = QUER_SET_C_N; return QUER_SET_C;
    default:  *n_out = 0; return NULL;
  }
}

static void quer_stop_action(void) {
  if (!g.quer_active) return;
  int qn = 0;
  const Stufe *items = quer_set_items(g.quer_set_name, &qn);
  if (!items || qn == 0) { g.quer_active = false; return; }
  Stufe st = items[g.quer_pos % qn];
  gutschreiben(st.typ, st.val);
  char buf[24];
  switch (st.typ) {
    case ST_SP:      snprintf(buf, sizeof(buf), "QUER +%dSP", st.val); break;
    case ST_MULTI:   snprintf(buf, sizeof(buf), "QUER +%dM", st.val); break;
    case ST_GIGA:    snprintf(buf, sizeof(buf), "QUER +%dG", st.val); break;
    case ST_MYSTERY: strcpy(buf, "MYSTERY!"); break;
    default:         strcpy(buf, "QUER"); break;
  }
  message_set(buf, 1800);
  vibes_short_pulse();
  reset_risiko_state();
  g.state = STATE_IDLE;
}

// ============================================================
// Update / Tick
// ============================================================
static void update_spinning(void) {
  uint32_t t = now_ms();
  bool all_stopped = true;
  for (int i = 0; i < 3; i++) {
    if (g.spin_stop_at[i] > t) {
      g.spin_offset[i]++;
      all_stopped = false;
    }
  }
  if (all_stopped) {
    evaluate_spin();
  }
}

static void update_quer(void) {
  uint32_t t = now_ms();
  if (t - g.quer_last_step_ms > 180) {
    int qn;
    const Stufe *items = quer_set_items(g.quer_set_name, &qn);
    if (qn > 0 && items != NULL) {
      g.quer_pos = (g.quer_pos + 1) % qn;
    }
    g.quer_last_step_ms = t;
  }
}

static void update_flash(void) {
  uint32_t t = now_ms();
  if (t - g.flash_last_ms > 320) {
    g.flash = !g.flash;
    g.flash_last_ms = t;
  }
}

static void tick_cb(void *ctx) {
  s_tick_timer = NULL;
  if (g.state == STATE_SPINNING) update_spinning();
  if (g.state == STATE_RISIKO && g.quer_active) update_quer();
  update_flash();
  redraw();
  schedule_tick();
}

static void schedule_tick(void) {
  if (s_tick_timer != NULL) return;
  s_tick_timer = app_timer_register(50, tick_cb, NULL);
}

// ============================================================
// Render
// ============================================================
static void draw_text_centered(GContext *ctx, const char *s, GRect r, GFont f, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static const char* stufe_label(const Stufe *st, char *buf, int buflen) {
  switch (st->typ) {
    case ST_NULL:    snprintf(buf, buflen, "-"); break;
    case ST_AUS:     snprintf(buf, buflen, "AUS"); break;
    case ST_CENT:    snprintf(buf, buflen, "%dc", st->val); break;
    case ST_SP:      snprintf(buf, buflen, "%dS", st->val); break;
    case ST_MULTI:   snprintf(buf, buflen, "%dM", st->val); break;
    case ST_GIGA:    snprintf(buf, buflen, "%dG", st->val); break;
    case ST_MA:      snprintf(buf, buflen, "MA"); break;
    case ST_MYSTERY: snprintf(buf, buflen, "?"); break;
  }
  return buf;
}

static GColor stufe_color(StufenTyp t) {
  switch (t) {
    case ST_GIGA:  return GColorRed;
    case ST_MULTI: return GColorChromeYellow;
    case ST_SP:    return GColorOxfordBlue;
    case ST_CENT:  return GColorDarkGray;
    case ST_AUS:   return GColorBlack;
    case ST_MA:    return GColorBlack;
    default:       return GColorBlack;
  }
}

static GColor stufe_text_color(StufenTyp t) {
  if (t == ST_GIGA || t == ST_MULTI) return GColorBlack;
  return GColorWhite;
}

// Zeichnet eine Risiko-Leiter (links oder rechts)
static void draw_leiter(GContext *ctx, GRect bounds, const Stufe *leiter, int n,
                        int8_t cur_idx, uint64_t pfeile, bool seite_is_left) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int step_h = bounds.size.h / n;
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_09);

  for (int i = 0; i < n; i++) {
    GRect r = GRect(bounds.origin.x, bounds.origin.y + i * step_h,
                    bounds.size.w, step_h);
    Stufe st = leiter[i];

    // Hintergrundfarbe je nach Stufentyp
    GColor bg = stufe_color(st.typ);
    GColor tx = stufe_text_color(st.typ);
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, r, 0, GCornerNone);

    // Hervorhebung wenn dies die aktuelle Stufe ist
    if (i == cur_idx) {
      graphics_context_set_stroke_color(ctx, GColorYellow);
      for (int k = 0; k < 2; k++) {
        GRect rr = GRect(r.origin.x + k, r.origin.y + k,
                         r.size.w - 2*k, r.size.h - 2*k);
        graphics_draw_rect(ctx, rr);
      }
    } else {
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      graphics_draw_rect(ctx, r);
    }

    // Pfeil-Marker: kleines Dreieck auf der rechten/linken Seite
    if ((pfeile >> i) & 1ULL) {
      GColor pcol = g.flash ? GColorWhite : (seite_is_left ? GColorRed : GColorGreen);
      graphics_context_set_fill_color(ctx, pcol);
      int py = r.origin.y + r.size.h / 2;
      if (seite_is_left) {
        GPoint pts[3] = {
          GPoint(r.origin.x + r.size.w - 5, py - 3),
          GPoint(r.origin.x + r.size.w - 1, py),
          GPoint(r.origin.x + r.size.w - 5, py + 3),
        };
        GPathInfo info = { 3, pts };
        GPath *path = gpath_create(&info);
        gpath_draw_filled(ctx, path);
        gpath_destroy(path);
      } else {
        GPoint pts[3] = {
          GPoint(r.origin.x + 4, py - 3),
          GPoint(r.origin.x, py),
          GPoint(r.origin.x + 4, py + 3),
        };
        GPathInfo info = { 3, pts };
        GPath *path = gpath_create(&info);
        gpath_draw_filled(ctx, path);
        gpath_destroy(path);
      }
    }

    // Label
    char buf[8];
    stufe_label(&st, buf, sizeof(buf));
    GRect tr = r;
    tr.origin.y -= 1;
    draw_text_centered(ctx, buf, tr, f, tx);
  }
}

// Symbol zeichnen (vereinfacht: farbige Box mit Text/Kuerzel)
static void draw_symbol(GContext *ctx, SymbolID s, GRect r) {
  GColor bg = GColorDarkGray;
  GColor fg = GColorWhite;
  char txt[6] = {0};
  switch (s) {
    case SYM_10C:  bg = GColorBlueMoon;     strcpy(txt, "10"); break;
    case SYM_20C:  bg = GColorBlueMoon;     strcpy(txt, "20"); break;
    case SYM_30C:  bg = GColorMediumSpringGreen; fg = GColorBlack; strcpy(txt, "30"); break;
    case SYM_40C:  bg = GColorMediumSpringGreen; fg = GColorBlack; strcpy(txt, "40"); break;
    case SYM_60C:  bg = GColorChromeYellow; fg = GColorBlack; strcpy(txt, "60"); break;
    case SYM_80C:  bg = GColorChromeYellow; fg = GColorBlack; strcpy(txt, "80"); break;
    case SYM_120C: bg = GColorOrange;       fg = GColorBlack; strcpy(txt, "120"); break;
    case SYM_170C: bg = GColorRed;          strcpy(txt, "170"); break;
    case SYM_K_ROT:  bg = GColorRed;        strcpy(txt, "K"); break;
    case SYM_K_GELB: bg = GColorYellow;     fg = GColorBlack; strcpy(txt, "K"); break;
    case SYM_4:    bg = GColorPurple;       strcpy(txt, "4"); break;
    case SYM_9:    bg = GColorPurple;       strcpy(txt, "9"); break;
    case SYM_3:    bg = GColorPurple;       strcpy(txt, "3"); break;
    case SYM_KARIERT: bg = GColorWhite;     fg = GColorBlack; strcpy(txt, "#"); break;
    case SYM_BLANK:
    default:       bg = GColorBlack;        strcpy(txt, "-"); break;
  }
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, r, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  graphics_draw_round_rect(ctx, r, 3);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GRect tr = GRect(r.origin.x, r.origin.y + (r.size.h - 14) / 2, r.size.w, 16);
  draw_text_centered(ctx, txt, tr, f, fg);
}

static void draw_walzen(GContext *ctx, GRect bounds) {
  // 3 Walzen nebeneinander, jeweils Symbole vertikal.
  // Walze 0 (links): 2 Symbole. Walze 1 (mitte): 1. Walze 2 (rechts): 2.
  int gap = 3;
  int reel_w = (bounds.size.w - 4 * gap) / 3;
  // Hintergrund
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 4, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  graphics_draw_round_rect(ctx, bounds, 4);

  uint32_t t = now_ms();
  bool spinning = (g.state == STATE_SPINNING);

  for (int reel = 0; reel < 3; reel++) {
    int rx = bounds.origin.x + gap + reel * (reel_w + gap);
    int ry = bounds.origin.y + gap;
    int rh = bounds.size.h - 2 * gap;
    int rows = (reel == 1) ? 1 : 2;
    int row_h = rh / rows;

    for (int row = 0; row < rows; row++) {
      GRect sr = GRect(rx + 2, ry + row * row_h + 1, reel_w - 4, row_h - 2);
      SymbolID s;
      if (spinning && g.spin_stop_at[reel] > t) {
        // Walze laeuft: zeige variable Symbole
        s = reel_at(reel, row + (int)(t / 60));
      } else {
        if (reel == 0) s = g.reel_l[row];
        else if (reel == 1) s = g.reel_m[0];
        else s = g.reel_r[row];
      }
      draw_symbol(ctx, s, sr);
    }
  }
}

static void draw_header(GContext *ctx) {
  GRect r = GRect(0, 0, SCR_W, HEADER_H);
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  graphics_context_set_text_color(ctx, GColorYellow);
  char buf[40];
  // Links: Münzspeicher
  snprintf(buf, sizeof(buf), "%ld.%02lde",
           (long)(g.muenzspeicher / 100), (long)(g.muenzspeicher % 100));
  graphics_draw_text(ctx, buf, f, GRect(2, 0, 80, HEADER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // Mitte: S/M/G
  snprintf(buf, sizeof(buf), "S%ld M%ld G%ld",
           (long)g.sonderspiele, (long)g.multispiele, (long)g.gigaspiele);
  graphics_draw_text(ctx, buf, f, GRect(60, 0, 140, HEADER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void draw_footer(GContext *ctx) {
  GRect r = GRect(0, BODY_Y1, SCR_W, FOOTER_H);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, r, 0, GCornerNone);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Zeile 1: Buttons
  const char *bA = "-";
  const char *bB = "-";
  const char *bC = "-";
  if (g.state == STATE_IDLE) {
    bA = "START";
    bB = "AUSZ";
    bC = "MUENZE";
  } else if (g.state == STATE_SPINNING) {
    bB = "STOP";
  } else if (g.state == STATE_RISIKO) {
    if (g.quer_active) {
      bB = "STOP!";
    } else {
      bA = "1:1";
      bB = "STOPP";
      if (g.risiko_can_teilen) bC = (quer_set_for_current() ? "TEI/QER" : "TEILEN");
      else if (quer_set_for_current()) bC = "QUER";
    }
  }
  graphics_context_set_text_color(ctx, GColorYellow);
  char line[40];
  snprintf(line, sizeof(line), "A:%s  B:%s  C:%s", bA, bB, bC);
  graphics_draw_text(ctx, line, f, GRect(0, BODY_Y1, SCR_W, 14),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Zeile 2: Message (verblasst)
  uint32_t t = now_ms();
  if (g.message[0] && t < g.message_until) {
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, g.message, f, GRect(0, BODY_Y1 + 11, SCR_W, 14),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void draw_quer_overlay(GContext *ctx) {
  if (!g.quer_active) return;
  int qn;
  const Stufe *items = quer_set_items(g.quer_set_name, &qn);
  if (qn == 0) return;

  // Box mittig im Body, ueberlagert Walzen
  int bx = WALZEN_X + 4;
  int by = BODY_Y0 + 12;
  int bw = WALZEN_W - 8;
  int bh = BODY_H - 24;
  GRect box = GRect(bx, by, bw, bh);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, box, 4, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  graphics_draw_round_rect(ctx, box, 4);

  GFont fb = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont fs = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  graphics_context_set_text_color(ctx, GColorYellow);
  graphics_draw_text(ctx, "QUER - B=STOP", fs, GRect(bx, by + 2, bw, 14),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  int item_h = (bh - 20) / qn;
  for (int i = 0; i < qn; i++) {
    GRect r = GRect(bx + 4, by + 18 + i * item_h, bw - 8, item_h - 2);
    bool lit = (i == g.quer_pos);
    GColor bg = lit ? GColorYellow : GColorOxfordBlue;
    GColor fg = lit ? GColorBlack : GColorLightGray;
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, r, 3, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_round_rect(ctx, r, 3);

    char buf[16];
    Stufe st = items[i];
    switch (st.typ) {
      case ST_SP:      snprintf(buf, sizeof(buf), "%d SP", st.val); break;
      case ST_MULTI:   snprintf(buf, sizeof(buf), "%d MULTI", st.val); break;
      case ST_GIGA:    snprintf(buf, sizeof(buf), "%d GIGA", st.val); break;
      case ST_MYSTERY: strcpy(buf, "MYSTERY"); break;
      default:         strcpy(buf, "?"); break;
    }
    GRect tr = r;
    tr.origin.y += (item_h - 14) / 2 - 2;
    draw_text_centered(ctx, buf, tr, fb, fg);
  }
}

static void draw_info_panel(GContext *ctx, GRect bounds) {
  // Box im mittleren Bereich, zeigt aktuelle Risiko-Stufe oder Status
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 4, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorYellow);
  graphics_draw_round_rect(ctx, bounds, 4);

  GFont fb = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont fs = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  if (g.state == STATE_RISIKO && !g.quer_active) {
    // Aktuelle Stufe gross anzeigen
    int n; const Stufe *l = current_leiter(&n);
    int8_t cur = current_idx();
    if (cur >= 0) {
      Stufe st = l[cur];
      char big[24];
      switch (st.typ) {
        case ST_CENT:  snprintf(big, sizeof(big), "%d Cent", st.val); break;
        case ST_SP:    snprintf(big, sizeof(big), "%d SP", st.val); break;
        case ST_MULTI: snprintf(big, sizeof(big), "%d MULTI", st.val); break;
        case ST_GIGA:  snprintf(big, sizeof(big), "%d GIGA", st.val); break;
        case ST_MA:    strcpy(big, "MULTI-AUS"); break;
        case ST_AUS:   strcpy(big, "AUS"); break;
        default:       strcpy(big, "?"); break;
      }
      graphics_context_set_text_color(ctx, GColorYellow);
      graphics_draw_text(ctx, big, fb,
                         GRect(bounds.origin.x, bounds.origin.y + 4,
                               bounds.size.w, 22),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter, NULL);
      // Hinweise je nach verfuegbaren Aktionen
      char hint[40];
      const char *quer_hint = quer_set_for_current() ? " C-lang=QUER" : "";
      if (g.risiko_can_teilen) {
        snprintf(hint, sizeof(hint), "A=1:1 B=STOPP C=TEIL%s", quer_hint);
      } else {
        snprintf(hint, sizeof(hint), "A=1:1 B=STOPP%s", quer_hint);
      }
      graphics_context_set_text_color(ctx, GColorLightGray);
      graphics_draw_text(ctx, hint, fs,
                         GRect(bounds.origin.x + 2, bounds.origin.y + 28,
                               bounds.size.w - 4, 18),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter, NULL);
      // Seite: L oder R
      char seite_buf[16];
      snprintf(seite_buf, sizeof(seite_buf), "Seite %c",
               (g.risiko_seite == 'L' ? 'L' : 'R'));
      graphics_context_set_text_color(ctx, GColorChromeYellow);
      graphics_draw_text(ctx, seite_buf, fs,
                         GRect(bounds.origin.x, bounds.origin.y + 48,
                               bounds.size.w, 16),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentCenter, NULL);
    }
  } else if (g.state == STATE_SPINNING) {
    graphics_context_set_text_color(ctx, GColorChromeYellow);
    graphics_draw_text(ctx, "SPINNING...", fb,
                       GRect(bounds.origin.x, bounds.origin.y + 8,
                             bounds.size.w, 22),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "B = Walze stoppen", fs,
                       GRect(bounds.origin.x, bounds.origin.y + 34,
                             bounds.size.w, 16),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  } else {
    // IDLE: zeige Statistik + Hinweise
    graphics_context_set_text_color(ctx, GColorYellow);
    graphics_draw_text(ctx, "Bally 493", fb,
                       GRect(bounds.origin.x, bounds.origin.y + 2,
                             bounds.size.w, 22),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
    char st[40];
    snprintf(st, sizeof(st), "%ld Spiele  %ld Gew.",
             (long)g.stats_spiele, (long)g.stats_gewinne);
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, st, fs,
                       GRect(bounds.origin.x, bounds.origin.y + 26,
                             bounds.size.w, 16),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "A=Start  C=Muenze", fs,
                       GRect(bounds.origin.x, bounds.origin.y + 46,
                             bounds.size.w, 16),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }
}

static void main_layer_update(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  draw_header(ctx);

  // Leitern
  GRect lr = GRect(0, BODY_Y0, LEITER_W, BODY_H);
  GRect rr = GRect(SCR_W - LEITER_W, BODY_Y0, LEITER_W, BODY_H);
  draw_leiter(ctx, lr, LEITER_LINKS, LEITER_LINKS_N,
              g.risiko_left_idx, g.pfeile_links, true);
  draw_leiter(ctx, rr, LEITER_RECHTS, LEITER_RECHTS_N,
              g.risiko_right_idx, g.pfeile_rechts, false);

  // Mittelteil aufteilen: Walzen oben (kompakt), Info-Box unten
  int mid_x = WALZEN_X + 2;
  int mid_w = WALZEN_W - 4;
  int walzen_h = 76;
  GRect wr = GRect(mid_x, BODY_Y0 + 4, mid_w, walzen_h);
  draw_walzen(ctx, wr);

  GRect info = GRect(mid_x, BODY_Y0 + 4 + walzen_h + 4,
                     mid_w, BODY_H - 12 - walzen_h);
  draw_info_panel(ctx, info);

  // Quer-Overlay ueber den ganzen Mittelteil wenn aktiv
  draw_quer_overlay(ctx);

  draw_footer(ctx);
}

static void redraw(void) {
  if (s_main_layer) layer_mark_dirty(s_main_layer);
}

// ============================================================
// Buttons
// ============================================================
static void btn_up_click(ClickRecognizerRef rec, void *ctx) {
  (void)rec; (void)ctx;
  if (g.state == STATE_IDLE) {
    start_spiel();
  } else if (g.state == STATE_RISIKO && !g.quer_active) {
    risiko_1zu1();
  }
  redraw();
}

static void btn_select_click(ClickRecognizerRef rec, void *ctx) {
  (void)rec; (void)ctx;
  if (g.state == STATE_IDLE) {
    // Auszahlen (vereinfacht: Muenzspeicher cash bleibt — wir haben keine
    // Tasche separat, der Speicher ist die "Tasche". Aktion macht ein
    // kurzes Feedback.)
    if (g.muenzspeicher > 0) {
      char buf[24];
      snprintf(buf, sizeof(buf), "GUTH %ld.%02ld",
               (long)(g.muenzspeicher / 100), (long)(g.muenzspeicher % 100));
      message_set(buf, 1500);
    } else {
      message_set("KEIN GUTH", 1000);
    }
  } else if (g.state == STATE_SPINNING) {
    request_stop_naechste();
  } else if (g.state == STATE_RISIKO) {
    if (g.quer_active) {
      quer_stop_action();
    } else {
      risiko_stopp();
    }
  }
  redraw();
}

static void btn_down_click(ClickRecognizerRef rec, void *ctx) {
  (void)rec; (void)ctx;
  // Kurzer Press auf DOWN
  if (g.state == STATE_IDLE) {
    // Muenze einwerfen (1 Euro)
    g.muenzspeicher += 100;
    message_set("+1.00 EUR", 800);
  } else if (g.state == STATE_RISIKO && !g.quer_active) {
    if (g.risiko_can_teilen) risiko_teilen();
  }
  redraw();
}

static void btn_down_long_click(ClickRecognizerRef rec, void *ctx) {
  (void)rec; (void)ctx;
  // Long-Press auf DOWN -> Quer-Start im Risiko
  if (g.state == STATE_RISIKO && !g.quer_active) {
    quer_start_action();
  }
  redraw();
}

static void click_config_provider(void *ctx) {
  (void)ctx;
  window_single_click_subscribe(BUTTON_ID_UP, btn_up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, btn_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, btn_down_click);
  // Long-Press DOWN fuer Quer-Spiel (600ms)
  window_long_click_subscribe(BUTTON_ID_DOWN, 600, btn_down_long_click, NULL);
}

// ============================================================
// App-Lifecycle
// ============================================================
static void window_load(Window *win) {
  Layer *root = window_get_root_layer(win);
  GRect bounds = layer_get_bounds(root);
  s_main_layer = layer_create(bounds);
  layer_set_update_proc(s_main_layer, main_layer_update);
  layer_add_child(root, s_main_layer);
}

static void window_unload(Window *win) {
  (void)win;
  if (s_main_layer) {
    layer_destroy(s_main_layer);
    s_main_layer = NULL;
  }
}

static void init(void) {
  // RNG seed
  srand((unsigned int)time(NULL));
  state_init();

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_set_background_color(s_window, GColorBlack);
  window_stack_push(s_window, true);

  schedule_tick();
}

static void deinit(void) {
  if (s_tick_timer) { app_timer_cancel(s_tick_timer); s_tick_timer = NULL; }
  state_save();
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}

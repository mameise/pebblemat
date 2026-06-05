# Bally 493 — Pebble Time 2 (Emery) App

Spielautomat im Stil des Bally Wulff 493 für die Pebble Time 2. C-Native-App,
keine externen Assets — alle Symbole werden mit Vektor-Boxen + Text gerendert.

## Features

- 3-Walzen-Spin mit zwei Gewinn-Linien (oben + unten durch die mittlere Walze)
- Komplette Risikoleiter links (CENT 20…170, AUS, SP 3/6/12, GIGA 2/4/9)
  und rechts (CENT 10…120, AUS, SP 2/5/10, MA, MULTI 12/24/50/100)
- Quer-Spiel (Lichtorgel in der Mitte) an den markierten Stufen
  (SP 6/SP 12 links, SP 5/SP 10/MULTI 12 rechts) mit drei verschiedenen Sets
  inkl. Mystery → GIGA 3/4/9 (gewichtet)
- Pfeile auf allen Stufen (außer AUS): wer ein 1:1-Risikospiel verliert,
  hinterlässt einen Pfeil — bei der nächsten Annäherung wird die Stufe
  übersprungen
- Persistent Storage: Münzspeicher, Sonder-/Multi-/Gigaspiele, Pfeile,
  Statistik bleiben über App-Schließen hinweg erhalten

## Buttons

| Button | Idle | Spinning | Risiko | Quer-Lichtorgel |
|--------|------|----------|--------|-----------------|
| UP     | Start (A) | – | 1:1 hoch | – |
| SELECT | Guthaben anzeigen | Walze stoppen | Stopp (kassieren) | STOPP (Wert nehmen) |
| DOWN kurz | Münze einwerfen (+1,00 €) | – | Teilen (falls möglich) | – |
| DOWN lang | – | – | Quer in Mitte (falls verfügbar) | – |
| BACK | App verlassen | – | – | – |

Beim Verlassen via BACK wird der Stand automatisch gespeichert.

## Build mit CloudPebble

1. Auf [cloudpebble.repebble.com](https://cloudpebble.repebble.com) anmelden
2. Neues Projekt anlegen: Type = **Pebble C SDK**, SDK = neueste Version
3. Im linken Sidebar unter SOURCE FILES: `main.c` öffnen, gesamten Inhalt
   durch den Inhalt von `src/main.c` aus diesem Repo ersetzen
4. Im SETTINGS:
   - **App Type**: Watchapp
   - **Target Platforms**: nur **Emery** aktivieren (Pebble Time 2)
   - **UUID**: kann unverändert bleiben oder neu generiert
5. **Compilation** → **Build** → bei Erfolg auf das Phone/die Watch installieren

## Build lokal (pebble-tool)

```bash
# einmalig: pebble-tool und SDK installieren
uv tool install pebble-tool --python 3.13
pebble sdk install latest

# im Repo-Verzeichnis
pebble build
pebble install --emulator emery   # zum Testen
pebble install --cloudpebble       # auf echte Watch (vorher pebble login)
```

## Wichtig: `package.json`

Liegt im Repo-Root. Hier kannst du:
- `displayName` anpassen
- `uuid` ändern (für eigene Distribution unbedingt neu generieren, z.B.
  mit `uuidgen`)

## Persistenz zurücksetzen

Wenn du den Spielstand zurücksetzen willst: App in der Pebble deinstallieren
und neu installieren. Persistent Storage wird dabei gelöscht.

## Bekannte Einschränkungen

- Keine Sound-Effekte (Pebble hat keinen Lautsprecher). Stattdessen
  Vibration bei Gewinn und Verlust.
- Auf dem kleinen Display sind die Stufen-Beschriftungen kompakt (z.B. "12S"
  für 12 Sonderspiele, "M50" für 50 Multispiele).
- Auszahlen ist hier vereinfacht: der "Münzspeicher" ist gleichzeitig deine
  Tasche. Du nimmst dein Guthaben mit, sobald du die App verlässt.

## Lizenz

Eigenes Projekt — keine offizielle Anbindung an Bally Wulff oder andere
Hersteller. Mechanik nach Erinnerung & Beschreibung nachgebaut, keine 1:1-
Kopie eines bestehenden Geräts.

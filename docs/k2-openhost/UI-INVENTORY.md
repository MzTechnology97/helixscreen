# Inventario UI stock di HelixScreen (riferimento per il redesign K2-OpenHost)

Sorgente analizzata: `\\wsl.localhost\Ubuntu\root\helixscreen` (sola lettura). Layout in `ui_xml/*.xml` e `ui_xml/components/*.xml`, logica in `src/ui/*.cpp`, `src/ui/panel_widgets/*`, `src/printer/ams_backend_cfs.cpp`, `include/ams_backend.h`.
Contesto target: Creality K2 Pro, firmware Kalico, CFS con modulo box Kalico ("Fork", schema flat `slots[]`, `api_version` 1).

Convenzioni:
- **Trigger** = nome del callback XML (`callback="..."`, registrato con `lv_xml_register_event_cb`) o funzione C++.
- **Gate** = binding XML (`bind_flag_if_*` = visibilita, `bind_state_if_*` = abilitazione) con subject e condizione, oppure il test C++ che nasconde/mostra.
- Gate ricorrenti: `nav_buttons_enabled` (1 = Moonraker connesso e Klippy ready), `job_holds_machine` (1 = un job possiede la macchina: stampa, pausa, preparazione host-side), `printer_connection_state` (2 = connesso), `ui_breakpoint` (solo layout, non riportato salvo quando nasconde funzioni), `show_widget_labels` (solo etichette).
- I file `k2oh_*` / `ui_k2oh_*` / `panel_widget_k2oh_*` (il redesign) sono esclusi; sono citati solo in fondo alla sezione 8 e nella sezione 12.

---

## 1. Barra di navigazione ed elementi globali

### 1.1 Barra di navigazione (`navigation_bar.xml`, `app_layout.xml`)

Pannelli in `app_layout.xml`: home (0), print_select (1), controls (2), filament (3), settings (4), advanced (5). Il pannello Print Select (1) **non ha un pulsante nella barra**: si raggiunge dal widget Home "Print Status" (Print Files / Recent Prints).

| Elemento | Cosa fa | Trigger | Gate | Rilevante per K2+Kalico CFS? |
|---|---|---|---|---|
| `nav_btn_edit_add` (+) | In modalita modifica Home apre il catalogo widget | `on_edit_add_widget_clicked` | visibile solo se `home_edit_mode`=1 | si: unico modo di aggiungere widget |
| `nav_btn_edit_done` (Done) | Esce dalla modalita modifica Home | `on_edit_done_clicked` | visibile solo se `home_edit_mode`=1 | si |
| `nav_btn_home` | Pannello Home; icona attiva se `active_panel`=0 | navigazione (C++) | disabilitato se `home_edit_mode`=1 | si |
| `nav_btn_controls` | Pannello Controls (2) | navigazione | disabilitato se `nav_buttons_enabled`!=1 o `home_edit_mode`=1; icona "disabled" se `nav_buttons_enabled`=0 | si |
| `nav_btn_filament` | Pannello Filament (3) | navigazione | come Controls | si: porta al CFS (Manage) |
| `nav_btn_settings` | Pannello Settings (4) | navigazione | disabilitato in edit mode | si |
| `nav_btn_advanced` | Pannello Advanced (5) | navigazione | disabilitato in edit mode | si |
| `nav_printer_badge` | Selettore stampanti (menu `printer_switch_menu.xml`: lista + "+ Add Printer" `printer_switch_add_cb`), con pallino stato connessione | navigazione/menu | visibile se `show_printer_switcher`=1 (Settings > Hardware > Printers > "Show Printer Switcher"); disabilitato in edit mode | no: una sola stampante (forse se la farm usa piu K2 da uno schermo) |

### 1.2 Header comune overlay (`header_bar.xml`, `overlay_panel.xml`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| `back_button` | Torna indietro (pop overlay) | `$back_button_callback` (default go_back) | sempre | si |
| `header_title_badge` | Badge immagine accanto al titolo (es. Spoolman nella Edit Slot) | - | visibile se `$title_badge_subject`!=0 | forse |
| `action_button` / `action_button_2` | Pulsanti azione di destra per overlay (Save, Probe, Start, Clear All, + Add...) | `$action_button_callback`, `$action_button_2_callback` | `$action_button_disabled_subject`/`_cond`, `$action_button_hidden_subject` | si |
| `estop_slot` | Spazio per e-stop nell'header | - | visibile se `$estop_visible_subject`=1 | si |

### 1.3 Elementi globali

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| E-stop FAB (`estop_fab` in `home_panel.xml`, `print_status_panel.xml`, `lock_screen.xml`) | Arresto di emergenza (M112) | `emergency_stop_clicked` (`ui_emergency_stop.cpp`) | visibile se `estop_visible`=1, cioe `job_holds_machine(lifecycle)` (stampa o preparazione) | si |
| E-stop in Controls (`estop_button_controls`) | Come sopra | `emergency_stop_clicked` | `estop_visible`=1 | si |
| Dialog conferma e-stop (`estop_confirmation_dialog.xml`) | "Emergency Stop?" Cancel / STOP | `estop_dialog_cancel_clicked`, `estop_dialog_confirm_clicked` | mostrato se Settings > Safety > "E-Stop Confirmation" attivo | si |
| Toast (`toast_notification.xml`) | Notifica temporanea con icona, messaggio, dettaglio, azione opzionale, chiusura | `toast_close_btn_clicked`; azione impostata da C++ | soglia da Settings > Safety > "On-screen Alerts" (`settings_min_toast_severity`) | si |
| Storico notifiche (`notification_history_panel.xml`) | Lista notifiche, "Clear All" (azione header, C++), stato vuoto | aperto da widget Notifications `status_notification_history_clicked` | `notification_has_entries` | si |
| Lock screen (`components/lock_screen.xml`) | Tastierino PIN 0-9, backspace, conferma; e-stop FAB anche da bloccato | `lock_digit_clicked`, `lock_backspace_clicked`, `lock_confirm_clicked` | attivo se PIN impostato (`lock_pin_set`) e lock manuale (widget Lock) o Auto-lock | si |
| Long-press bloccato su Home | Un long-press mentre lo schermo e bloccato non entra in edit mode | `HomePanel` (controlla `LockManager::is_locked()`) | - | si |
| Dim / Sleep / Screensaver | Oscura, spegne o mostra screensaver dopo inattivita; tocco per risvegliare | gestore display (impostazioni in Display & Sound) | `settings_has_dimming` per dim/luminosita | si |
| Banner aggiornamento (`components/upgrade_banner.xml`) | "Update" / chiudi | `on_upgrade_banner_update`, `on_upgrade_banner_dismiss` | testo `upgrade_banner_text` | forse: aggiornamenti dal fork |
| Tour di benvenuto (`tour_tooltip_card.xml`) | Skip / Next | C++ | primo avvio o "Replay Welcome Tour" | no |
| Overlay backdrop (`overlay_backdrop.xml`) | Sfondo scuro dietro overlay | - | `overlay_backdrop_visible` | si (infrastruttura) |
| Overlay memoria (`memory_stats_overlay.xml`) | RSS/Peak/Private/Delta/Pressure, tasto M | debug | solo debug | no |
| Tastierino numerico (`numeric_keypad_panel.xml`) | Inserimento valori (temperature custom ecc.), punto decimale | C++ | `keypad_allow_decimal` abilita "." | si |
| Wizard primo avvio (`wizard_*.xml`) | Lingua, Wi-Fi, connessione Moonraker, tipo stampante, heater, fan, LED, sensore filamento, AMS identify, input shaper, touch calibration, telemetria, riepilogo | `on_next_clicked`, `on_back_clicked`, ecc. | solo primo avvio / factory reset | forse: solo setup iniziale |

**Conteggio sezione 1:** 8 voci barra + 4 header + 14 globali = 26.

---

## 2. Home panel

### 2.1 Griglia, pagine e modalita modifica (`home_panel.xml`, `ui_panel_home.cpp`, `grid_edit_mode.cpp`, `ui_widget_catalog_overlay.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Carousel pagine | Pagine di widget scorrevoli (max `MAX_PAGES`=8), pagina principale = 0 | swipe; frecce sx/dx create in C++ (`update_arrow_visibility`) | frecce dove lo swipe non e disponibile | si |
| Tile "+ Add page" | Ultima tile del carousel: aggiunge pagina e ci naviga | `on_add_page_clicked` | visibile se pagine < 8 | si |
| Long press griglia | Entra in modalita modifica (`GridEditMode::enter`) | `on_home_grid_long_press` (+ `on_home_grid_pressed/pressing/released/clicked`) | solo se Settings > System > Touch & Input > "Allow Home Screen Editing" (`settings_home_edit_mode_enabled`) e schermo non bloccato | si |
| Selezione widget | Tap su widget in edit mode mostra cornice con angoli e maniglie | `GridEditMode::handle_click` | edit mode | si |
| Sposta widget | Long-press + drag, anteprima snap su griglia | `handle_drag_start/move/end` | edit mode | si |
| Ridimensiona widget | Drag dei bordi (maniglie sx/dx/alto/basso) entro min/max colspan/rowspan del catalogo | `handle_resize_move/end`, `commit_resize_with_snap` | solo widget ridimensionabili (`is_selected_widget_resizable`) | si |
| Rimuovi widget (cestino) | Pill cestino in alto a destra | `remove_selected_widget` | widget selezionato | si |
| Configura widget (ingranaggio) | Pill in alto a sinistra, apre la configurazione del widget | `configure_selected_widget` -> `PanelWidget::on_edit_configure()` | solo widget con `has_edit_configure()`: camera, clog_detection, fan_stack, fan, print_status, temp_graph, temp_stack, thermistor, favorite_macro, power_device | si |
| Delete Page | Pulsante in edit mode, conferma "Delete Page / Remove this page and all its widgets?" | `set_delete_page_callback` -> `modal_confirm` | nascosto sulla pagina principale | si |
| Done / + (navbar) | Vedi 1.1 | `on_edit_done_clicked`, `on_edit_add_widget_clicked` | `home_edit_mode` | si |
| Catalogo widget (`widget_catalog_overlay.xml`) | Elenco di tutti i widget; tap su riga = piazza widget (`place_widget_from_catalog`) | C++ | riga attenuata se gia piazzato (non multi-istanza) o se gate hardware=0, con suffisso "(hint)" | si |
| Catalogo: Reset | "Reset Home Screen?" ripristina layout di default | `on_catalog_reset` | sempre | si |
| Widget gate | Widget con `gate_subject`=0 non mostrati in Home | `setup_widget_gate_observers` | subject di gate per widget (tabella 2.2) | si |

### 2.2 Catalogo widget (`panel_widget_registry.cpp`) e azioni

Colonne extra: **Def** = abilitato di default; **Dim** = col x row default (min..max).

| id (nome catalogo) | Def | Dim | Cosa mostra / fa | Trigger | Gate (catalogo + XML) | Rilevante? |
|---|---|---|---|---|---|---|
| `printer_image` (Printer Image) | si | 2x2 (1x1..4x3) | Immagine stampante, overlay disconnesso/shutdown, barra tipo; tap apre Printer Manager | `printer_manager_clicked_cb` | nessuno; info bar `printer_info_visible` | si |
| `print_status` (Print Status) | si | 2x2 (2x1..4x3) | Idle: Print Files, Reprint Last, Recent Prints, Job Queue; compatto; dettagliato idle (Reprint Last); in stampa: badge Paused, preparazione (barra, messaggio, ETA), progresso, dettagliato attivo (arco, layer, tempo, filamento, temperature con chevron multi-tool); tap apre Print Status | `print_card_clicked_cb`, `library_files_cb`, `library_last_cb`, `library_recent_cb`, `library_queue_cb`, `print_status_nozzle_chevron_cb`; configura: `print_status_layout_library_cb`/`_detailed_cb` + "Show Sections" | disabilitato se `printer_connection_state`!=2; `print_status_view`, `*_hidden`, `print_status_idle_has_last` | si: e l'accesso a Print Select |
| `shutdown` (Shutdown/Reboot) | no | 1x1 | Apre `shutdown_modal` | `shutdown_clicked_cb` | `platform_host_power_supported` | forse: spegnimento host CM5 |
| `lock` (Lock Screen) | no | 1x1 | Se PIN: blocca schermo; altrimenti apre Security settings | `lock_screen_clicked_cb` | icona da `lock_pin_set` | forse |
| `power_device` (Power, multi) | no | 1x1 | Toggle device Moonraker (singolo o "tutti"), stato/lock; non configurato: picker | `power_device_clicked_cb`; configure | `power_device_count`; disabilitato se `moonraker_connection_state`!=1 | forse: solo se Moonraker [power] |
| `network` (Network) | no | 1x1 (..2x1) | Icona Wi-Fi/Ethernet; tap apre Network Settings | `network_clicked_cb` | `home_network_icon_state` | si |
| `firmware_restart` (Firmware Restart) | no | 1x1 | FIRMWARE_RESTART | `firmware_restart_clicked` | nessuno | si |
| `tool_switcher` (Tool Switcher) | no | 1x1 (..2x2) | Pill tool / picker tool | `tool_pill_cb`, `tool_compact_cb` | nessuno | no: K2 monotool |
| `led` (LED Light) | si | 1x1 (..2x1) | Toggle luce (la descrizione catalogo cita long-press per controllo completo, ma `led_widget.cpp` registra solo il click) | `light_toggle_cb` | `led_controllable`; disabilitato se `led_command_in_flight`=1 | si |
| `led_controls` (LED Controls) | no | 1x1 | Apre LED control overlay | `on_led_controls_clicked` | `led_controllable` | si |
| `fan_stack` (Fan Speeds, multi) | si | 1x1 (..3x2) | Part/Hotend/Aux; tap apre Fan control overlay o picker; configure (modalita, icona: `fan_stack_picker.xml`) | `on_fan_stack_clicked` | connessione | si |
| `fan` (Fan, multi) | no | 1x1 (..2x1) | Singola ventola; tap: fan control; senza ventola: picker | `fan_widget_clicked_cb` | connessione | si |
| `temperature` (Nozzle Temperature) | si | 1x1 (..2x2) | Temperatura ugello; tap apre Temp graph overlay (Nozzle) | `temp_clicked_cb` (HeaterTempWidget) | connessione | si |
| `nozzle_temps` (Nozzle Temperatures) | no | 1x2 (..2x3) | Tutti gli estrusori con barre; tap apre graph Nozzle/Bed | C++ | nessuno | no: monotool |
| `bed_temperature` | no | 1x1 (..2x2) | Piatto; tap: graph Bed | `bed_temp_clicked_cb` | connessione | si |
| `chamber_temperature` | no | 1x1 (..2x2) | Camera; tap: graph Chamber | `chamber_temp_clicked_cb` | `printer_has_chamber` | si: K2 ha sensore camera |
| `temp_stack` (Temperatures) | no | 1x1 (..3x2) | Ugello/piatto/camera impilati; tap per riga apre graph; configure | `temp_stack_nozzle_cb`, `temp_stack_bed_cb`, `temp_stack_chamber_cb`, `temp_carousel_page_cb` | riga camera: `printer_has_chamber` | si |
| `thermistor` (Temperature Sensors, multi) | no | 1x1 (..2x1) | Sensore singolo o carousel; picker sensore; configure (selezione multipla) | `thermistor_clicked_cb` | `temp_sensor_count` | si |
| `temp_graph` (Temperature Graph, multi) | no | 2x2 (..6x4) | Grafico; tap apre Temp graph overlay GraphOnly; configure (`temp_graph_config_modal.xml`) | `on_temp_graph_widget_clicked` | connessione | si |
| `preheat` (Preheat) | no | 3x1 (2x1..4x1) | Split button materiale (PLA/PETG/ABS/TPU) applica preriscaldo o cooldown; target tool; tap riga ugello/piatto apre graph | `preheat_widget_apply_cb`, `preheat_widget_changed_cb`, `preheat_tool_target_cb`, `preheat_nozzle_tap_cb`, `preheat_bed_tap_cb` | connessione | si |
| `ams` (Multi-Filament System Status) | no | 1x1 (..4x2) | `ams_mini_status`: stato slot; tap apre pannello Multi-Filament (`navigate_to_ams_panel`) | click in `ui_ams_mini_status.cpp` | `ams_slot_count` | si |
| `bypass` (Bypass) | no | 1x1 (..2x1) | Toggle bypass/spool esterno, icona on/off, pallino colore + materiale spool esterno | `bypass_widget_clicked_cb` -> `BypassToggleController::toggle()` | catalogo: `ams_supports_bypass`; disabilitato se `job_holds_machine`=1; riga attiva se `ams_bypass_active`=1 | si: vedi 8.6 |
| `active_spool` (Active Spool) | no | 1x1 (..4x2) | Spool attiva (canvas, materiale, marca, peso); tap: Edit Slot dello slot caricato oppure editor spool esterno (slot -2) | C++ `handle_clicked` | connessione | si |
| `filament` (Filament Sensor) | si | 1x1 (..2x1) | Stato sensore: Loaded/Empty/Off | nessuno | `filament_sensor_count`; testo da `filament_runout_detected` | si |
| `humidity` (Humidity) | no | 1x1 (..2x2) | Umidita camera | nessuno | `humidity_sensor_count` | forse: umidita CFS passa da AMS env |
| `width_sensor` (Width Sensor) | no | 1x1 (..2x2) | Diametro filamento | nessuno | `width_sensor_count` | no |
| `favorite_macro` (Macro Button, multi) | no | 1x1 (..2x1) | Esegue macro (parametri/conferma); non configurato: apre `favorite_macro_config_modal` (Macro / Appearance / Options "Require Confirmation?") | `favorite_macro_clicked_cb`; configure | connessione | si |
| `macros` (Macros) | no | 1x1 | Apre Macro panel | `macros_widget_clicked_cb` | connessione | si |
| `motion` (Motion) | no | 1x1 | Apre Motion panel | `motion_widget_clicked_cb` | connessione | si |
| `clock` (Digital Clock) | no | 2x1 (..3x3) | Ora, data, uptime | nessuno | `clock_size_mode` | forse |
| `control_buttons` (Print Controls) | no | 2x1 | Pausa/Riprendi e Stop | `on_print_control_primary`, `on_print_control_stop` | `print_control_primary_enabled`, `print_control_stop_enabled` | si |
| `job_queue` (Job Queue) | no | 2x2 (2x1..4x3) | Coda Moonraker; tap apre `job_queue_modal` (Pause/Start coda, chiudi) | `on_job_queue_widget_clicked`, `on_jq_modal_toggle_queue`, `on_jq_modal_close` | `jq_size_mode` | forse: farm |
| `tips` (Tips) | si | 4x2 (2x1..6x2) | Suggerimenti a rotazione; tap mostra dettaglio | `tip_text_clicked_cb` | nessuno | no |
| `clog_detection` (Clog Detection) | no | 1x1 (..2x2) | Misuratore clog/flusso; tap: `BufferStatusModal`; configure: `clog_detection_config_modal` (Source Auto/Encoder/Flowguard/AFC, Mode, Detection Length, Danger Zone) | `on_clog_detection_widget_clicked` | `clog_meter_mode` | forse: dipende se CFS pubblica clog meter |
| `print_stats` (Print Stats) | no | 2x2 (2x1..3x2) | Statistiche storico; tap alterna lifetime/settimanale | `print_stats_clicked_cb` | `print_stats_size_mode` | forse |
| `gcode_console` (GCode Console) | no | 1x1 | Apre Console | `gcode_console_clicked_cb` | connessione | si |
| `camera` (Camera) | no | 2x2 (..4x3) | Feed webcam; tap fullscreen (`camera_fullscreen.xml`, chiudi `on_camera_fullscreen_close`); configure: rotazione 0/90/180/270, flip H/V | `on_camera_clicked`, `on_cam_rotate_*`, `on_cam_flip_*` | solo build `HELIX_HAS_CAMERA` | si: camera K2 su crowsnest |
| `notifications` (Notifications) | si | 1x1 (..2x1) | Campanella con badge contatore/severita; tap apre storico | `status_notification_history_clicked` | badge se `notification_count`>0 | si |

**Conteggio sezione 2:** 13 voci griglia/edit + 38 widget di catalogo (esclusi i 5 `k2oh_*`) = 51.

---

## 3. Print Select (`print_select_panel.xml`, `print_file_detail.xml`, `ui_panel_print_select.cpp`, `ui_print_start_controller.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Source selector Printer / USB | Sorgente file stampante o chiavetta USB | `on_print_select_source_printer`, `on_print_select_source_usb` | visibile se USB presente e non accessibile via Moonraker (`print_source_usb_present`, `print_source_moonraker_usb_access`) | forse |
| View toggle | Vista card / lista | `on_print_select_view_toggle` | `print_select_view_mode` | si |
| Card file (`print_file_card.xml`) | Miniatura, nome, tempo, peso; cartelle e "cartella su" | tap = dettaglio; long-press = elimina (conferma) | card view | si |
| Riga lista (`print_file_list_row.xml`) | Nome, stato storico (in stampa, N successi, fallito, annullato), dimensione, data, tempo | tap = dettaglio | dimensione/data nascoste se `ui_breakpoint`<3 | si |
| Ordinamento | Header Filename / Size / Modified / Time con frecce su/giu | `on_print_select_header_filename/_size/_modified/_print_time` | Size/Modified nascosti su schermi piccoli | si |
| Banner "Recently Printed" | Contesto quando aperto da Recent Prints | C++ (`hide_context_banner`) | - | si |
| Stato vuoto | "No files available for printing" | - | nessun file | si |
| Dettaglio: header/back | Chiude dettaglio | `on_print_detail_back_clicked`, `on_print_select_detail_backdrop` | - | si |
| Dettaglio: anteprima | Miniatura o viewer G-code 3D/2D, caricamento con progresso | C++ | `detail_gcode_loading`, `detail_gcode_progress`; modalita da Settings > Printing > G-code Preview | si |
| Dettaglio: metadati | Tempo, peso filamento, altezza, layer, layer height, tipo filamento | binding `selected_*` | - | si |
| Dettaglio: stato storico | Ultimo esito del file | C++ | `history_status_row` | si |
| Sliced colors | Mostra colori dello slicer nel viewer | `on_toggle_sliced_colors` | visibile solo con viewer G-code e primo frame pronto | forse |
| Card "Bypass active" | "Bypass active - this print runs on the external spool." | - | visibile se `ams_bypass_active`=1 | si: bypass Fork |
| Card FILAMENTS (mapping) | Righe tool -> slot (swatch atteso -> slot scelto), icona mismatch, icona tool vuoti, chevron se rimappabile; tap apre Filament Mapping modal | C++ (`ui_filament_mapping_card.cpp`); help `on_color_card_remap_help` | `filament_mapping_visible`, `color_card_remappable`, `filament_mismatch`, `empty_tools_warning`, `color_card_remap_help_visible`, `detail_mapping_ready` | si: CFS 4 slot |
| Help remap | Se manca plugin HelixPrint: "Filament remapping is unavailable" con "Install plugin" / "Not now" | `on_color_card_remap_help` -> `show_remap_help_modal` | `RemapBlock::NeedsPlugin` | forse: CFS ha remap nativo (`RemapStrategy::Native`) |
| Filament Mapping modal (`filament_mapping_modal.xml`) | Lista tool con picker slot (`filament_slot_picker_row`), Cancel/OK | `on_modal_cancel_clicked`, `on_modal_ok_clicked` | - | si |
| PRINT OPTIONS | Toggle pre-stampa: Auto/Adaptive Bed Mesh, Quad Gantry Level, Z-Tilt Adjust, Clean Nozzle, Nozzle Priming, Record Timelapse, opzioni da printer DB (es. "AI detection") | `ui_pre_print_options_renderer.cpp` | `has_any_preprint_options`; ogni opzione da capacita stampante e plugin | forse: dipende dalle macro Kalico |
| Stima preparazione | Testo `preprint_estimate_text` | - | - | forse |
| Delete | "Delete File?" conferma | `on_print_select_delete_button` | nascosto se `print_source_is_usb`=1 | si |
| Print | Avvia flusso di stampa | `on_print_select_print_button` | disabilitato se `print_select_can_print`=0 | si |
| Preflight "Check filament" (`preflight_check_modal.xml`) | Tool -> slot con swatch atteso/montato, "EMPTY", gravita; Remap... / Cancel / Print Anyway | `ui_preflight_check_modal.cpp` | slot richiesto definitivamente vuoto | si |
| Gate di avvio (in ordine) | `printer_stopping_command` ("File Will Stop the Printer", blocca), `insufficient_spool_weight` / `insufficient_lane_weight` ("Not Enough Filament"), `bypass_engaged_lane_print` ("Bypass Is Active"), `unaccounted_toolhead_filament` ("Filament In The Toolhead"), `required_filament_present` ("No Filament Detected"), `unresolved_tools`, `material_compatibility` ("Color Mismatch", "Filament Grade Mismatch", "Material Mismatch") con "Start Anyway" | `PrintStartController::run_gates_from` (`print_start_checks.cpp`) | per gate | si |
| Rifiuto avvio | "Cannot start print: printer is ..." | `PrintStartController::initiate` | stato stampante | si |
| Ripristino mapping | Dopo la stampa ripristina la mappa tool->slot (persistita) | `restore_filament_mapping`, `recover_pending_remap` | backend con remap | si |
| Plugin install modal (`plugin_install_modal.xml`) | Proposta plugin HelixPrint (tracking fasi, "Don't ask again", Install / Not Now, copia comando SSH) | `on_plugin_install_clicked`, `on_plugin_install_cancel`, `on_plugin_copy_clicked`, `on_plugin_install_ok` | plugin assente | forse |
| Storico (accesso) | Print History da Advanced (sezione 10) e Reprint Last/Recent da Home | - | - | si |

Nota: non esiste "rename" file nel codice stock (nessun callback); solo delete.

**Conteggio sezione 3:** 26 voci.

---

## 4. Print Status overlay (`print_status_panel.xml`, `components/print_status_preview_card.xml`, `ui_panel_print_status.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Header + e-stop | Titolo "Print Status", slot e-stop | - | `estop_visible` | si |
| Anteprima | Miniatura o viewer G-code (`preview_stack`) | C++ | modalita G-code Preview | si |
| View toggle | Alterna vista completa/ghost (tutti i layer solidi) | `on_view_toggle` | visibile se `gcode_viewer_mode`!=0 | forse |
| Exclude objects | Pannello mappa + lista oggetti (`exclude_object_map.xml`, `exclude_object_side_list.xml`), conferma `exclude_object_modal` | `on_print_status_objects`, `on_exclude_map_close`, `on_exclude_side_list_close` | `exclude_objects_available` | si |
| Overlay esito | Print Complete! / Print Cancelled / Print Failed; tap chiude | `on_print_status_dismiss_overlay` | `show_complete_overlay`, `show_cancelled_overlay`, `show_error_overlay` | si |
| Badge Paused + motivo | "Print Paused" + `print_pause_reason` | - | `show_paused_overlay`, `print_pause_reason_visible` | si |
| Barra progresso, layer, oggetti, filamento usato | `print_progress_display`, `print_layer_text`, `print_objects_text`, `print_filament_used_text` | - | oggetti: `exclude_objects_available` | si |
| Nome file, trascorso, rimanente, ETA, % | `print_display_filename`, `print_elapsed`, `print_remaining`, `print_eta`, `print_progress_text` | - | - | si |
| Display message (M117) | `display_message` | - | `display_message_visible` | si |
| Preparing overlay | Spinner, fase `print_start_message`, barra `preparing_progress`, ETA | - | `preparing_visible` | si |
| Temp card unificata | Ugello/piatto/camera con stato; tap apre Temp graph GraphOnly | `on_temp_card_clicked` | riga camera: `printer_has_chamber` | si |
| Riga Speed / Flow | Valori correnti; tap apre Tune | `on_print_status_tune` | nascosta se `ui_breakpoint` 1 o 2 | si |
| Indicatore sensore filamento | `filament_runout_scoped` | - | nascosto se nessun sensore o se AMS con tool corrente >=0 | si |
| Tool change status | Testo cambio tool in corso | - | `toolchange_visible` | si: cambio slot CFS |
| AMS current tool (`ams_current_tool.xml`) | Tool corrente + swatch + sensore; tap | `on_ams_current_tool_clicked` | `ams_slot_count`>0 e `ams_current_tool`>=0 | si |
| Riga ventole | Part / Hotend / Aux; tap apre Fan control overlay | `on_print_status_fans_clicked` | `print_status_fans_fit`; aux `print_status_aux_fan_present` | si |
| Light | Toggle luce | `on_print_status_light` | `led_controllable`; disabilitato con `led_command_in_flight` | si |
| Timelapse | Attiva/disattiva timelapse | `on_print_status_timelapse` | `printer_has_timelapse`; disabilitato se `print_controls_enabled`=0 | forse |
| Pause / Resume | Pulsante primario (icona/etichetta dinamiche) | `on_print_control_primary` (`print_control_buttons.cpp`) | `print_control_primary_enabled` (richiede macro Pause/Resume) | si |
| Tune | Apre Print Tune | `on_print_status_tune` | `print_controls_enabled` | si |
| Cancel | Stop con conferma `print_cancel_confirm_modal` ("Stop Print?") | `on_print_control_stop` | nascosto a stampa finita (`print_outcome`!=0); `print_control_stop_enabled` | si |
| Reprint | Ristampa (file effettivo originale) | `on_print_status_reprint` | visibile solo a stampa finita | si |
| Abort progress modal | Durante cancel con escalation: messaggio + "EMERGENCY STOP" | `on_abort_emergency_stop` | Settings > Safety > Cancel Escalation | si |
| Completion modal (`print_completion_modal.xml`) | Titolo, file, durata, stima, layer, filamento; OK | `on_print_complete_ok` | Settings > Safety > Print Completion Alert = Alert | si |
| Runout guidance | Vedi sezione 11 | `ui_filament_runout_handler.cpp` | runout in pausa | si |
| Spaghetti detection | "Print issue detected": Abort / Reduce Sensitivity / Resume | `spaghetti_detection_modal.xml` | AI detection | no |

**Conteggio sezione 4:** 26 voci.

---

## 5. Print Tune overlay (`print_tune_panel.xml`, `ui_print_tune_overlay.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Print Speed | Valore impostato + reale, -10/-5/+5/+10 % | `on_tune_speed_adjust` | `nav_buttons_enabled` | si |
| Flow Rate | Valore + reale, -10/-5/+5/+10 % | `on_tune_flow_adjust` | idem | si |
| Reset Speed & Flow to 100% | Reset | `on_tune_reset_clicked` | idem | si |
| Z-Offset: valore + indicatore | `tune_z_offset_display` | - | - | si |
| Passo Z | 0.05 / 0.025 / 0.01 / 0.005 | `on_tune_z_step` | `z_step_N_active` | si |
| Farther / Closer | Babystep Z (icone dipendono da Z Movement) | `on_tune_z_adjust` | - | si |
| Hint Z-offset | "Temporary...unless saved on Controls panel" / "auto-saved by firmware" / "Temporary..." / "...a tool change resets it" | - | `z_offset_tune_hint` 0..3 | si |
| Save Z-offset (tune) | callback registrato | `on_tune_save_z_offset` | non presente nel layout XML (salvataggio da Controls) | forse |

Ventole: non nel Tune; si regolano dalla riga ventole del Print Status (Fan control overlay, sezione 6).

**Conteggio sezione 5:** 8 voci.

---

## 6. Controls panel e overlay (`controls_panel.xml`, `ui_panel_controls.cpp`)

### 6.1 Pannello

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Card Position | X/Y/Z con colore homed; tap apre Motion | `on_controls_quick_actions` | `x_homed`/`y_homed`/`z_homed` | si |
| Riga Z-Offset | Valore; tap apre Print Tune | `on_zoffset_tune` | - | si |
| Save Z-Offset | Salva (modal "Save Z-Offset? ... restart Klipper and CANCEL any active print") | `on_controls_save_z_offset` -> `on_z_offset_save`/`on_z_offset_cancel` | `z_offset_can_save`, `gcode_z_offset`!=0; disabilitato se `job_holds_machine` | si |
| Motion | Apre Motion | `on_controls_quick_actions` | - | si |
| Emergency Stop | E-stop | `emergency_stop_clicked` | `estop_visible` | si |
| Card Temperature | Tap: overlay ugello | `on_nozzle_temp_clicked` | - | si |
| Ugello / matita | Riga apre Nozzle temp; matita apre tastierino target | `on_nozzle_temp_clicked`, `on_nozzle_target_edit` | - | si |
| Piatto / matita | Bed temp / tastierino | `on_bed_temp_clicked`, `on_bed_target_edit` | - | si |
| Camera / matita | Chamber temp / tastierino | `on_chamber_temp_clicked`, `on_chamber_target_edit` | `printer_has_chamber` | si |
| Temperature secondarie | Lista sensori extra; tap | `handle_secondary_temps_clicked` | C++ | si |
| Card Cooling | Slider ventola parte + %; tap card apre Fan control | `on_controls_fan_slider`, `on_controls_cooling` | - | si |
| Ventole secondarie | Lista; tap | `handle_secondary_fans_clicked` | C++ | si |
| Bed Mesh | Apre Bed Mesh panel | `on_calibration_bed_mesh` | - | si |
| Z Calibration | Apre Z-Offset calibration | `on_calibration_zoffset` | disabilitato se `job_holds_machine` | si |
| QGL | QUAD_GANTRY_LEVEL | `on_controls_qgl` | `printer_has_qgl`; disab. se job o `controls_operation_in_progress` | no: K2 non ha QGL |
| Z-Tilt | Z_TILT_ADJUST | `on_controls_z_tilt` | `printer_has_z_tilt`; idem | forse: dipende da config Kalico |
| Bed Screws | Apre Screws tilt | `on_calibration_screws` | `printer_has_screws_tilt` | no |
| Motors Off | Conferma e M84 | `on_calibration_motors` | disabilitato se `motors_enabled`=0 | si |
| Light | Toggle luce | (light_button) | `led_controllable`; connessione; `led_command_in_flight` | si |
| Quick Actions Home All/X/Y/XY/Z | Homing | `on_controls_home_all/_x/_y/_xy/_z` | disab. se `controls_operation_in_progress`; card visibile se `macro_header_visible` | si |
| Macro 1..4 | Pulsanti macro configurati (Settings > Printing > Macro Buttons > Quick Button 1-4) | `on_controls_macro` | `macro_N_visible`; disab. se job o `macro_N_available`=0 | si |

### 6.2 Overlay

| Overlay / elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Motion (`motion_panel.xml`) | Card posizione (Z reale "Act:" se `motion_z_actual_visible`), jog pad XY, modalita Fine/Coarse/Turbo, Z su/giu grande/piccolo, QGL / Z-Tilt, header e-stop | `on_jog_mode_fine/_coarse/_turbo`, `on_motion_z_button`, `on_motion_qgl`, `on_motion_z_tilt`, `emergency_stop_clicked` | `nav_buttons_enabled`; QGL/Z-tilt come sopra | si |
| Nozzle temp (`nozzle_temp_panel.xml`) | Grafico, attuale/target, preset Off/M0/M1/M2/spool, Custom... (tastierino), selettore estrusore | `on_heater_preset_clicked`, `on_nozzle_custom_clicked` | `preset_spool` visibile con spool attiva | si |
| Bed temp (`bed_temp_panel.xml`) | Idem per piatto | `on_heater_preset_clicked`, `on_bed_custom_clicked` | - | si |
| Chamber temp (`chamber_temp_panel.xml`) | Attuale/target/modo; preset e Custom | `on_heater_preset_clicked`, `on_heater_custom_clicked` | preset/custom solo se `printer_has_chamber_heater` | forse: dipende se K2 Pro espone heater camera |
| Temp graph overlay (`temp_graph_overlay.xml`) | Chip sensori + grafico; strisce controllo ugello/piatto/camera con preset (camera 40/50/60 C) e Custom | `on_temp_graph_preset_clicked`, `on_temp_graph_custom_clicked` | `temp_graph_mode` 0..3; camera preset se `printer_has_chamber_heater` | si |
| Fan control (`fan_control_overlay.xml`, `fan_dial.xml`) | Dial per ventola con on/off | `on_fan_dial_switch_changed` | - | si |
| Bed Mesh (`bed_mesh_panel.xml`) | Canvas 3D/2D, card mesh corrente (nome, dimensione, max/min, range), profili (carica, rinomina, elimina), Probe (header) | `on_bed_mesh_calibrate_clicked`, `on_profile_N_clicked/_rename/_delete` | `bed_mesh_available`, `bed_mesh_profile_count` | si |
| Bed mesh calibrate modal | Probing (barra, EMERGENCY STOP), nome profilo, Start/Cancel, errore | `on_bed_mesh_calibrate_start/_cancel`, `on_bed_mesh_emergency_stop` | `bed_mesh_calibrate_state` | si |
| Bed mesh rename / save config | Rinomina; "Save Printer Configuration? ... restart Klipper" | `on_bed_mesh_rename_confirm/_cancel`, `on_bed_mesh_save_config_yes/_no` | - | si |
| Z-Offset calibration (`calibration_zoffset_panel.xml`) | Paper test: Warm Bed, Start, probing, regolazione +/-.005/.01/.05/.1/1, Accept/Abort, saving, complete, error/Retry, warming | `on_zoffset_warm_bed_toggled`, `on_zoffset_start_clicked`, `on_zoffset_z_adjust`, `on_zoffset_accept_clicked`, `on_zoffset_abort_clicked`, `on_zoffset_done_clicked`, `on_zoffset_retry_clicked` | `zoffset_cal_state`; Warm Bed se `printer_has_heater_bed` | si (con Cartographer: forse, dipende dal flusso probe) |
| Screws tilt (`screws_tilt_panel.xml`) | Probing viti, risultati, share QR, re-probe | `screws_tilt_start_cb`, `_cancel_cb`, `_done_cb`, `_reprobe_cb`, `_share_cb`, `_retry_cb` | `printer_has_screws_tilt` | no |
| Light: LED control overlay (`led_control_overlay.xml`) | Selettore strip, colore preset (8) + custom, Turn Off, luminosita, effetti, WLED (on/off, luminosita, preset), macro | `led_color_preset_cb`, `led_custom_color_cb`, `led_native_turn_off_cb`, `led_brightness_changed_cb`, `led_wled_toggle_cb` | `led_strip_selector_visible`, `led_native_visible`, `led_color_visible`, `led_effects_visible`, `led_wled_visible`, `led_macro_visible` | si: luce K2 (macro o pin) |

**Conteggio sezione 6:** 21 voci pannello + 12 overlay = 33.

---

## 7. Filament panel (`filament_panel.xml`, `ui_panel_filament.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Temperature ugello / piatto / camera | Attuale/target; tap per impostare target | `on_filament_nozzle_target_tap`, `on_filament_bed_target_tap`, `on_filament_chamber_target_tap` | camera: `printer_has_chamber` | si |
| Stato | `filament_status` | - | `ui_breakpoint`>=2 | si |
| Grafico temperatura | Ugello+piatto | C++ | - | si |
| Card Multi-Filament: mini status | `ams_mini_status` | - | `ams_manage_row` visibile se AMS o multi-tool | si |
| Manage | Apre pannello Multi-Filament (o overview se multi-unita) | `filament_manage_slots_cb` -> `navigate_to_ams_panel()` | AMS monotool | si: accesso principale al CFS |
| Tool dropdown | Selezione estrusore (al posto di Manage) | `on_extruder_dropdown_changed` | multi-tool | no |
| Riga spool esterno | Spool esterno (canvas, materiale, colore); tap apre Edit Slot per slot -2 | `on_external_spool_edit` | **visibile solo se NON c'e AMS e NON multi-tool** (C++ ~riga 1642): su K2 con CFS e nascosta | forse: lo spool esterno CFS si gestisce dal pannello AMS |
| Preset materiale x4 | Tap: seleziona materiale e temperature; long-press: picker catalogo filamenti per modificare il preset (con "Reset" ai default) | `on_filament_preset_pla/_petg/_abs/_tpu`, `..._hold` | - | si |
| Preset spool | Preset dalla spool attiva | `on_filament_preset_spool` | spool attiva | si |
| Cool Down | Spegne riscaldamento | `on_filament_cooldown` | visibile se `filament_nozzle_heating`=1 | si |
| Load / Unload / Purge | Operazioni filamento (macro standard o backend AMS) | `on_filament_load`, `on_filament_unload`, `on_filament_purge` | disabilitati se `filament_safety_warning_visible`, `filament_operation_in_progress`, `filament_load_disabled`/`filament_unload_disabled` | si |
| Lunghezza 5/10/25 mm | Lunghezza estrusione | `on_filament_extrude_length_5mm/_10mm/_25mm` | - | si |
| Extrude / Retract | Estrude/retrae a mano | `on_filament_extrude`, `on_filament_retract` | come sopra (senza load/unload_disabled) | si |
| Avviso sicurezza | Es. ugello freddo | - | `filament_safety_warning_visible` | si |

**Conteggio sezione 7:** 14 voci.

---

## 8. Multi-Filament (AMS) panel e tutto cio che raggiunge

Accessi: Filament > Manage; widget Home `ams`; Settings > Hardware > Multi-Filament System Management (solo overlay gestione); Print Status AMS current tool.

### 8.1 Pannello (`ams_panel.xml`, `ui_panel_ams.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Back + titolo + suffisso sistema | "Multi-Filament: <logo> <nome>" | `on_header_back_clicked` | suffisso se `ui_breakpoint`>=2 | si |
| Backend selector | Segmenti per piu backend | `on_backend_segment_selected` | piu backend | no |
| Unit card + slot (`ams_unit_detail`, `ams_slot_view.xml`) | Slot con materiale, badge stato, badge tool, badge %; env indicator | tap slot = menu contestuale (8.2) | `nav_buttons_enabled` | si |
| Endless spool arrows | Frecce backup tra slot | C++ | nascoste se `get_endless_spool_config()` vuoto: **su CFS sempre nascoste** (config vuota per scelta) | no |
| Endless status (`ams_endless_status.xml`) | Testo stato auto-refill | - | nascosto se `ams_endless_state`=0 | si: stato runout swap (solo lettura) |
| Filament path canvas | Percorso slot -> hub -> toolhead, animazioni | vedi 8.4 | - | si |
| Bypass spool node | Spool esterno sul percorso (8.6) | `on_bypass_spool_clicked` | `bypass_node_visible_for` | si (Fork) |
| Sidebar | Vedi 8.3 | - | `nav_buttons_enabled` | si |
| Loading error modal | Vedi 8.10 | `show_loading_error_modal` | `AmsAction::ERROR` | si |

### 8.2 Tap slot e menu contestuale (`ams_context_menu.xml`, `ui_ams_context_menu.cpp`)

Tap su slot (`AmsPanel::on_slot_clicked` -> `handle_slot_tap`) o sul tubo nel canvas (`on_path_slot_clicked`) apre sempre lo stesso menu (mai un load diretto).

| Voce | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Intestazione "Filament" / "Tool" | Etichetta colonna | - | `ams_slot_mounts_tool` | si |
| Load (o "Mount") | Load con preriscaldo via sidebar (`handle_load_with_preheat`); rifiuta se backend occupato | `ams_context_load_cb` | disabilitato se `ams_slot_can_load`=0 (`decide_can_load`: sistema occupato, unload toolhead, slot senza filamento, stampa) | si: CFS Fork `T<n>` |
| Unload / Eject / Recover / Unmount | Etichetta da `decide_unload_mode`; Unload via sidebar `handle_unload(slot)` | `ams_context_unload_cb` | disabilitato se `ams_slot_is_loaded`=0 | si: `BOX_UNLOAD` |
| Hint force_move | "Eject needs [force_move]..." | - | solo QIDI | no |
| Hint unload bloccato | `ams_slot_unload_hint` | - | `ams_slot_unload_hint_visible` | si |
| Select slot | Seleziona gate | `ams_context_gate_select_cb` | `backend->supports_gate_select()`: CFS no | no |
| Check slot | Rilettura slot | `ams_context_gate_check_cb` | `supports_gate_check()`: **CFS solo se schema Flat e dialetto Fork** -> `_BOX_RFID_READ_SLOT SLOT=<n>` | si |
| Select Spool | Edit Slot aperto sul picker Spoolman | `ams_context_spoolman_cb` | `printer_has_spoolman`=1 | forse |
| Scan QR Code | QR scanner overlay, assegna spool Spoolman | `ams_context_scan_qr_cb` | `printer_has_spoolman`=1 (camera) | forse |
| Spool Info | Apre Edit Slot (8.5) | `ams_context_edit_cb` | sempre | si |
| Clear Spool | Cancella metadati slot | `ams_context_clear_spool_cb` | slot con spoolman_id>0 o materiale | si |
| Tool dropdown | (riga presente, mai mostrata: codice commentato come rimosso) | `ams_context_tool_changed_cb` | sempre nascosto | no |
| Backup (endless) dropdown | Slot di backup | `ams_context_backup_changed_cb` | `decide_show_backup_row`: su CFS (ReadOnly, nessuna relazione) nascosto | no |
| Endless status | Stato auto-refill | - | `ams_endless_state` | si |

### 8.3 Sidebar (`components/ams_sidebar.xml`, `ui_ams_sidebar.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Slot corrente + loaded card | Materiale, peso, clog meter (arco + modo) | - | peso se `ams_current_has_weight`; clog se `clog_meter_mode`!=0 | si |
| Stato / dettaglio azione | `ams_action_detail` | - | - | si |
| Step progress | Stepper operazione load/unload | C++ | operazione in corso | si |
| Bypass switch | Attiva/disattiva bypass (8.6) | `ams_sidebar_bypass_toggled` -> `BypassToggleController::toggle()` | riga visibile se `ams_supports_bypass`=1; checked da `ams_bypass_active`; disabilitato se `job_holds_machine`=1; pulsanti nascosti se `ams_action`!=0 | si |
| Unload | Unload slot attivo (pianificazione `plan_unload`, rifiuta se occupato o in stampa) | `ams_sidebar_unload_clicked` | visibile se `ams_is_filament_system`=1; disab. `ams_sidebar_unload_disabled` | si |
| Reset | `clear_fault` + `reset()`; CFS: `BOX_ERROR_CLEAR`; etichetta da `reset_button_label()` ("Reset" su CFS) | `ams_sidebar_reset_clicked` | nascosto se `ams_is_tool_changer`=1 | si |
| Check slots | `check_all_gates()`; CFS Fork: `refresh_rfid` -> `BOX_INFO_REFRESH ADDR=<n> NUM=15` per unita | `ams_sidebar_check_gates_clicked` | visibile solo se `supports_gate_check()` (CFS: Flat+Fork) | si |
| Settings | Apre Device Operations overlay (8.7) | `ams_sidebar_settings_clicked` | sempre | si |

### 8.4 Canvas percorso (`ui_filament_path_canvas.cpp`)

| Interazione | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Tap tubo/linea slot | Menu contestuale slot | `on_path_slot_clicked` | slot valido | si |
| Tap toolhead | Se lo slot ha `mapped_tool`>=0: `offer_tool_change` (immediato o conferma "Change Tool"; se gia attivo apre menu slot); altrimenti menu slot | `on_path_toolhead_clicked` | verdetto `decide_tool_change` (stampa, occupato) | si |
| Tap hub/selettore | Menu selettore (`ams_selector_menu.xml`): Home selector, Check slots, Servo su/move/giu, Move selector prev/next, Gear motor synced, Recover | `on_path_hub_clicked` -> `ams_selector_*_cb` | solo se `supports_gate_select()`: **CFS no** | no |
| Tap buffer | `BufferStatusModal` (tipo buffer, spool motor, gear sync, clog, flow, AFC distanza, meter) | `on_buffer_clicked` | buffer presente nel canvas | forse |
| Tap bypass spool | Menu spool esterno (8.6) | `on_bypass_spool_clicked` | nodo visibile | si |

### 8.5 Edit Slot overlay (`ams_edit_overlay.xml`, `ui_ams_edit_overlay.cpp`)

| Campo / azione | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Header Save / Back | Salva (`commit_slot_edit` o `commit_external_spool_edit`); su CFS Fork scrive `_BOX_SLOT_SET SLOT= MATERIAL= COLOR= BRAND= NAME= SPOOLMAN_ID=` | `ams_edit_save_cb`, `ams_edit_back_cb` | `ams_edit_save_disabled`, `ams_edit_save_hidden`; badge Spoolman `ams_edit_is_managed` | si |
| Remap Tool | Dropdown tool->slot | `ams_edit_tool_changed_cb` | `can_write_mapping_table(backend)`; CFS `owns_tool_mapping_table`=true: invia `BOX_MODIFY_TN Tx=Ty` (nessun ramo Fork: da verificare su Kalico) | forse |
| Spool card | Colore, identita, ID Spoolman, residuo %, temperature; tap apre dettagli | `ams_edit_card_clicked_cb` | marchio/ID se managed | si |
| Change Filament | Picker spool (ricerca, loading, empty+retry, no match, lista, "Not in your spools? Set it up" -> spool wizard) | `ams_edit_change_filament_cb`, `ams_edit_picker_search_cb`, `ams_edit_picker_retry_cb`, `ams_edit_setup_entry_cb` | `ams_edit_view`=1, `edit_picker_state` | forse: richiede Spoolman |
| Scan QR Code | Assegna spool da QR | `ams_edit_scan_qr_cb` | - | forse |
| Dettagli: Color | Anteprima, matita custom, griglia swatch rapidi | `ams_edit_custom_color_cb`, `ams_edit_quick_swatch_cb` | `ams_edit_view`=2 | si |
| Dettagli: catalogo filamento | Vendor / Type / prodotto (`filament_catalog_selector`), aggiungi custom, modifica prodotto (`filament_product_edit_modal`) | `catalog_select_vendor_changed_cb`, `catalog_select_type_changed_cb`, `catalog_row_clicked_cb`, `catalog_add_custom_cb`, `catalog_row_edit_cb` | - | si |
| Remaining (g), Spool wt (g) | Peso residuo e tara | `ams_edit_detail_field_changed_cb` | - | si |
| Price, Location, Lot/Batch Nr, Notes | Campi Spoolman | idem | solo se `ams_edit_is_managed`=1 | forse |
| Save to Spoolman | Crea/aggiorna su Spoolman | switch | `printer_has_spoolman`=1 | forse |
| Print Label | Stampa etichetta | `ams_edit_print_label_cb` | `label_printer_configured`=1 | no |
| Vista colore | Preset, HSV, hex, Select | `ams_edit_color_swatch_cb`, `ams_edit_color_hex_changed_cb`, `ams_edit_color_apply_cb` | `ams_edit_view`=3 | si |

### 8.6 BYPASS / spool esterno (end to end)

**Elementi UI**

| Elemento | Cosa fa | Trigger | Gate |
|---|---|---|---|
| Widget Home `bypass` | Toggle | `bypass_widget_clicked_cb` -> `BypassToggleController::toggle()` | catalogo `ams_supports_bypass`; disab. `job_holds_machine` |
| Switch sidebar | Toggle | `ams_sidebar_bypass_toggled` -> stesso controller | `ams_supports_bypass`; disab. `job_holds_machine` |
| Switch Device Operations "Bypass Mode" | Toggle | `on_ams_device_ops_bypass_toggled` -> stesso controller, poi `sync_from_backend()` | `ams_device_ops_supports_bypass`=1 e `ams_device_ops_hw_bypass_sensor`=0; disab. `job_holds_machine` |
| Riga "Controlled by hardware sensor" + pill Active/Inactive | Solo stato | - | `ams_device_ops_hw_bypass_sensor`=1 (CFS: mai, il backend non imposta `has_hardware_bypass_sensor`) |
| "Enable Bypass Controls" | Forza i controlli bypass se il firmware non lo dichiara (`set_ams_force_bypass_controls`) | `on_ams_force_bypass_controls_toggled` | visibile solo se `ams_device_ops_fw_supports_bypass`=0 |
| "Always Show Bypass Spool" | Mostra nodo anche a bypass spento | `on_ams_always_show_bypass_spool_toggled` | solo AFC (`ams_device_ops_is_afc`) |
| Nodo bypass spool nel canvas (`ui_bypass_spool_widget.cpp`) | Spool esterno: colore, materiale, anello "attivo" | tap -> `show_external_spool_menu` | `bypass_node_visible_for(backend)` = `bypass_available_for(supports_bypass)` e (attivo o non-AFC o always_show) |
| Menu spool esterno (`ui_external_spool_menu.cpp` -> `AmsContextMenu::show_for_external_spool`) | Load/Unload nascosti; Spool Info (EDIT), Select Spool (SPOOLMAN, se Spoolman), Scan QR (se Spoolman), Clear Spool (se assegnato: "External spool cleared") | callback del menu contestuale | Clear se spool esterno ha spoolman_id>0 o materiale |
| Edit spool esterno | Edit Slot per slot -2; salvataggio `AmsState::commit_external_spool_edit` (persistito in settings) | menu o widget Active Spool | - |
| Card "Bypass active" in Print detail | Avviso | - | `ams_bypass_active`=1 |
| Gate avvio `bypass_engaged_lane_print` | "Bypass Is Active ... this print uses AMS slots" Start Anyway | `print_start_checks.cpp` | bypass attivo e stampa che richiede >1 lane |
| Riga spool esterno nel Filament panel | Edit spool esterno | `on_external_spool_edit` | solo senza AMS: **nascosta sul K2 con CFS** |
| Lane_data OrcaSlicer | All'attivazione del bypass pubblica lo spool esterno come lane extra (`lane{total_slots+1}`) | `AmsState` -> `AmsBackendCfs::publish_external_spool_lane` | `supports_bypass` e override store |

**Flusso del toggle** (`src/ui/ui_bypass_toggle_controller.cpp`):

```cpp
if (job_holds_machine(state)) { NOTIFY_WARNING(lv_tr("Bypass cannot be changed while printing")); return; }
...
if (info.has_hardware_bypass_sensor) { NOTIFY_WARNING(lv_tr("Bypass controlled by sensor")); return; }
if (backend->is_bypass_active()) { AmsError error = backend->disable_bypass(); ... return; }
if (should_unload_before_bypass(info, backend->allows_implicit_chaining())) {
    pending_bypass_enable_ = true; arm_backend_observer();
    AmsError error = backend->unload_active_filament(); ...  // poi poll_pending_engage() -> enable_now()
}
enable_now(backend);   // backend->enable_bypass()
```

`should_unload_before_bypass` (`include/ams_types.h`): `allows_implicit_chaining && info.current_slot >= 0 && info.filament_loaded`. CFS non sovrascrive `allows_implicit_chaining()` (base = `true`), quindi se uno slot del CFS e caricato il controller fa prima l'unload e poi abilita il bypass quando la lane risulta libera.

**Chi decide `supports_bypass` sul CFS** (`src/printer/ams_backend_cfs.cpp`, `handle_status_update`):

```cpp
if (is_flat) {
    external_slot_index_ = external_index;
    bypass_declared_ = false;
    const bool fork_bypass =
        macro_variant_ == CfsMacroVariant::Fork && external_slot_index_ >= 0;
    if (system_info_.supports_bypass != fork_bypass) { ... system_info_.supports_bypass = fork_bypass; }
} else if (!system_info_.supports_bypass) {
    system_info_.supports_bypass = true;   // stock: holder esterno + sensore toolhead
}
```

- `macro_variant_ = Fork` solo se `detect_fork_dialect(state)`: `helix::json_util::safe_int(box_json, "api_version", 0) == 1` (schema flat).
- `external_index = find_external_slot_index(state)`: indice della voce `slots[]` con `"external": true` (`-1` se assente). Il Fork registra `T<external_index>` (external_slot = max slot fisico + 1, es. `T4` con un CFS).
- `parse_flat_box_status` imposta sempre `info.supports_bypass = false`; la convergenza sopra lo rialza solo per Fork + voce external.
- Override utente: `helix::bypass_available_for(fw)` = `fw || SettingsManager::get_ams_force_bypass_controls()` (`ams_bypass_policy.cpp`). Il subject `ams_supports_bypass` (sidebar, widget, catalogo) usa questo valore (`AmsState`, ~riga 1864).

**Comandi inviati (CFS)**

| Azione | Fork / Flat (Kalico) | Stock CFS (schema non flat) | Flat non identificato |
|---|---|---|---|
| `enable_bypass()` | `T<external_slot_index_>` via `dispatch_action_script` (azione LOADING, attended: il firmware scalda, va al cestino, attende fino a 30 s l'inserimento a mano). Rifiuta se `macro_variant_ != Fork` o `external_slot_index_ < 0` ("This CFS firmware exposes no external spool slot") | `BOX_ENABLE_CFS_PRINT ENABLE=0` + dichiarazione persistita (`set_bypass_declared(true)`); l'utente alimenta a mano | `NOT_SUPPORTED` (supports_bypass=false) |
| Precondizione comune | `bypass_available_for(supports_bypass)`; rifiuta se `filament_loaded && current_slot >= 0` ("Unload the CFS filament before enabling bypass") | idem | - |
| `disable_bypass()` | se attivo: `unload_gcode(Fork)` = `BOX_UNLOAD` (il firmware sceglie il ramo external da `loaded_slot`: scalda, retrae, taglia, attende rimozione) | `BOX_ENABLE_CFS_PRINT ENABLE=1`, dichiarazione azzerata | - |
| `is_bypass_active()` | `current_slot == -2` (da `loaded_slot` == indice external nel payload) | `current_slot == -2` o `bypass_declared_` | - |
| Load slot -2 (`do_load_filament(-2)`) | **non supportato**: il ramo bypass esclude Fork, `load_gcode(-2, Fork)` restituisce "" -> `invalid_slot`. L'unico ingresso e `enable_bypass()` | `LOAD_MATERIAL` se esiste, altrimenti feed 80 mm (`bypass_load_gcode`) | `reject_if_flat_schema` |
| Unload slot -2 | `BOX_UNLOAD` (stesso comando delle bay) | `QUIT_MATERIAL` + retract 80 mm, o fallback taglio+retract | `reject_if_flat_schema` |
| Load di una bay con bypass dichiarato | nessun re-arm (Flat escluso) | prepone `BOX_ENABLE_CFS_PRINT ENABLE=1` | - |

**Esito per K2 + Kalico (Fork):** il bypass funziona solo se il payload `box` ha `api_version: 1` **e** contiene in `slots[]` una voce con `"external": true`. In quel caso: riga bypass in sidebar, widget Home `bypass`, sezione Bypass nella Device Operations e nodo spool esterno nel canvas compaiono; ON = `T<n_external>`, OFF = `BOX_UNLOAD`. Se manca la voce external, nessun controllo bypass (a meno dell'override "Enable Bypass Controls", che pero fa fallire `enable_bypass()` con NOT_SUPPORTED). Il toggle e sempre bloccato durante un job (`job_holds_machine`).

### 8.7 Device Operations overlay (`ams_device_operations.xml`, `ui_ams_device_operations_overlay.cpp`)

| Riga | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| System info | "System: <tipo> - v<versione>" | - | - | si |
| Quick Actions: Home | `backend->reset()`; CFS = `BOX_ERROR_CLEAR` | `on_ams_device_ops_home` | - | si |
| Quick Actions: Recover | `backend->recover()`; CFS non-K1 (anche Fork) = `BOX_ERROR_RESUME_PROCESS` | `on_ams_device_ops_recover` | - | forse: da verificare che Kalico definisca il comando |
| Quick Actions: Abort | `backend->cancel()`; CFS = `CANCEL_PRINT` | `on_ams_device_ops_abort` | - | si |
| Bypass Mode (virtuale) | Toggle bypass (8.6) | `on_ams_device_ops_bypass_toggled` | `ams_device_ops_supports_bypass`=1 e `ams_device_ops_hw_bypass_sensor`=0; disab. `job_holds_machine` | si (Fork) |
| Bypass Mode (sensore) | Pill stato | - | `ams_device_ops_hw_bypass_sensor`=1 | no |
| Auto-Heat on Load | Solo informativa | - | `supports_auto_heat_on_load()` (CFS: true) | si |
| Unloads After Print | Setting AFC | `on_ams_afc_unload_after_print_toggled` | solo AFC | no |
| Always Show Bypass Spool | Setting | `on_ams_always_show_bypass_spool_toggled` | solo AFC | no |
| Keep Spool Info on Eject | Setting | `on_ams_keep_spool_info_toggled` | `printer_reports_spool_ids()` (CFS: base false) | no |
| Nota remember_spool AFC | Testo | - | AFC | no |
| Enable Bypass Controls | Override bypass | `on_ams_force_bypass_controls_toggled` | visibile se `fw_supports_bypass`=0 | forse: solo se manca la voce external |
| QIDI Eject Distance / Velocity | Slider | `on_ams_qidi_eject_distance_changed`, `..._velocity_changed` | QIDI | no |
| Reset Endless Spool | Conferma e azzera backup | `on_ams_reset_endless_spool_clicked` | `get_endless_spool_capabilities().editable()` (CFS: ReadOnly -> nascosto) | no |
| Status | `ams_device_ops_status` | - | - | si |
| Sezioni device (lista) | Righe per sezione -> `ams_device_section_detail.xml` (azioni toggle/bottoni/slider/dropdown) | `on_section_row_clicked` | solo sezioni con azioni; `get_device_sections()` | **no su CFS: il backend non sovrascrive `get_device_sections()` (vuoto), quindi le sue azioni non compaiono** |
| No backend | "No Multi-Filament System connected." | - | `ams_device_ops_has_backend`=0 | si |

Azioni device CFS definite ma **non raggiungibili** dalla UI stock (sezioni vuote): `refresh_rfid` ("Refresh RFID", `BOX_INFO_REFRESH`), `toggle_auto_refill` (Fork: `_BOX_SET_RUNOUT_SWAP ENABLE=0/1`), `nozzle_clean` (`BOX_NOZZLE_CLEAN`), `comm_test` (riquery stato), e gli switch Fork `clog_detection_enabled` (`_BOX_SET_CLOG_DETECTION`), `unload_after_print_enabled` (`_BOX_SET_UNLOAD_AFTER_PRINT`), `rfid_insert_reading_enabled` (`_BOX_SET_RFID_INSERT_READING`), `rfid_startup_reading_enabled` (`_BOX_SET_RFID_STARTUP_READING`). Solo `refresh_rfid` e raggiungibile indirettamente via "Check slots" (`check_all_gates`).

### 8.8 Environment overlay e dryer (`ams_environment_overlay.xml`, `ams_environment_indicator.xml`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Indicatore env (unita/slot area) | Temp, umidita (colore per stato), drying; tap apre overlay | `on_env_indicator_clicked` | `$visible`, `$humidity_visible`, `$drying_active` | si: CFS `has_environment_sensors()`=true |
| Readout temperatura (-> target se drying) | `ams_env_overlay_temp_text` | - | target se `ams_env_overlay_drying_active` | si |
| Umidita | `ams_env_overlay_humidity_text` | - | `ams_env_overlay_humidity_visible` | si |
| Drying in corso + barra | Testo e progresso | - | `drying_active` | no |
| Controlli dryer | Preset, Temp C, Minutes, Start/Stop | `on_ams_env_start_stop_clicked` | `ams_env_overlay_dryer_visible` = `get_dryer_info().supported` (CFS: base false) | no |
| "No dryer available..." | Consiglio | - | `ams_env_overlay_no_dryer_visible` | si (mostrato su CFS) |
| Material Comfort | Righe 0..3 materiali ok/warn/bad per umidita | - | `ams_env_comfort_N_visible/_status` | si |

### 8.9 Multi-unit overview (`ams_overview_panel.xml`, `ui_panel_ams_overview.cpp`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Ingresso | `navigate_to_ams_panel()`: overview se `info.is_multi_unit()`, altrimenti pannello singolo | - | CFS flat: 4 bay per unita, 1 CFS = 1 unita | forse: solo con 2+ CFS concatenati |
| Unit cards (`ams_unit_card.xml`) | Logo, nome, barre slot, conteggio, runout, env | tap = dettaglio unita nello stesso overlay | logo se non `ams_cards_compact`; runout se `ams_filament_runout` | forse |
| System path canvas | Percorso multi-unita | - | - | forse |
| Dettaglio unita | Slot + path canvas della singola unita | - | - | forse |
| Back | Indietro (o torna all'overview) | `on_ams_overview_back_clicked` | - | forse |
| Sidebar | Come 8.3 | - | - | forse |
| Bypass node | Stesso menu spool esterno (`show_external_spool_menu`) | - | come 8.6 | forse |

### 8.10 Modali AMS

| Modale | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Loading error (`ams_loading_error_modal.xml`) | Messaggio, hint, percorso fault AFC; Retry = `reset()` + load con preriscaldo dello slot; chiusura = `clear_fault(slot)` | `on_modal_ok_clicked` (Retry), `on_modal_cancel_clicked` | `AmsAction::ERROR`; percorso fault solo `afc_fault_segment`!=0 | si |
| Recover state (`ams_recover_state_modal.xml`) | Imposta slot selezionato e caricato senza movimento | dal menu selettore "Recover" | `supports_recover_with_state()` (CFS: base false) | no |
| Buffer status (`buffer_status_modal.xml`) | Vedi 8.4 | `on_buffer_clicked`, widget clog | - | forse |
| Conferma "Recover MMU state?" | Recover Happy Hare | selettore | `supports_gate_select` | no |
| Conferma cambio tool | "Change Tool" / "Change Tool While Paused" | `offer_tool_change` | verdetto | si |

### 8.11 Copertura del redesign gia presente (solo nota)

`ui_k2oh_cfs_view.cpp` copre: back, chip stato, chip ambiente (apre Environment overlay), chip spool esterno e `bypass_row` (menu spool esterno), "Check all" e "Settings" (inoltrano click a `btn_check_gates` e `btn_settings` della sidebar stock), card slot selezionabili, percorso, switch "Auto-swap at spool end" (`toggle_auto_refill`), Load/Unload, "Reread RFID" (`check_gate(slot)`), Edit, More; il bypass viene inoltrato allo switch stock (`bypass_switch`). Widget k2oh: job, temps, cfs, light, fans.

**Conteggio sezione 8:** 9 (pannello) + 14 (menu) + 8 (sidebar) + 5 (canvas) + 12 (edit) + 13 (bypass UI) + 17 (device ops) + 7 (env) + 7 (overview) + 5 (modali) = 97.

---

## 9. Settings panel e sotto-overlay (`settings_panel.xml`, `ui_panel_settings.cpp`, `ui_settings_*.cpp`)

### 9.1 Pannello

| Riga | Apre | Trigger | Rilevante? |
|---|---|---|---|
| Display & Sound | 9.2 | `on_display_sound_clicked` | si |
| Printer | Printer Manager overlay (9.3) | `on_printer_clicked` | si |
| Printing | 9.4 | `on_printing_clicked` | si |
| Hardware & Devices | 9.5 | `on_hardware_clicked` | si |
| Safety & Notifications | 9.6 | `on_safety_clicked` | si |
| System | 9.7 | `on_system_clicked` | si |
| Help & About | 9.8 | `on_help_clicked` | si |

### 9.2 Display & Sound (`settings_display_sound_overlay.xml`)

| Opzione | Valori / azione | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Language | lingue | `on_language_changed` | - | si |
| Timezone | lista | `on_timezone_changed` | - | si |
| Time Format | 12/24 h | `on_time_format_changed` | - | si |
| Animations | toggle | `on_animations_changed` | - | si |
| System Keyboard | toggle | `on_system_keyboard_changed` | solo Android | no |
| Keep Navigation Bar | toggle | `on_keep_navbar_changed` | solo Android | no |
| Widget Labels | toggle (`show_widget_labels`) | `on_widget_labels_changed` | - | si |
| Scroll Buttons | toggle | `on_page_scroll_buttons_changed` | - | si |
| Bed Mesh Render | Auto / 3D View / 2D Heatmap | `on_bed_mesh_mode_changed` | - | si |
| Screen Rotation | Normal / 90 / 180 / 270 (dopo restart) | `on_display_rotation_changed` | `settings_rotation_available` | forse |
| Dark Mode | toggle | `on_dark_mode_changed` | disab. se `settings_dark_mode_available`=0 | si |
| Theme Colors | Theme preview overlay: preset, dark mode, anteprima, Apply, Edit (theme editor, Save As) | `on_theme_settings_clicked`, `on_theme_preset_changed`, `on_apply_theme_clicked`, `on_edit_colors_clicked`, `on_theme_save_as_confirm` | `theme_apply_disabled` | forse |
| Brightness | slider | `on_brightness_changed`, `on_brightness_commit` | `settings_has_dimming` | si |
| Screen Dim | Never / 30 s / 1 / 2 / 5 / 10 min | `on_dim_changed` | `settings_has_dimming` | si |
| Display Sleep | Never / 1 / 5 / 10 / 20 / 30 min | `on_sleep_changed` | - | si |
| Screensaver | Off / Flying Toasters / Starfield / 3D Pipes; Test Screensaver | `on_screensaver_changed`, `on_test_screensaver` | test se tipo!=0 | forse |
| Sleep While Printing | toggle | `on_sleep_while_printing_changed` | - | si |
| Sounds | toggle | `on_sounds_changed` | `printer_has_speaker` | forse |
| Volume | slider | `on_volume_changed`, `on_volume_commit` | suoni attivi | forse |
| UI Sounds | toggle | `on_ui_sounds_changed` | suoni attivi | forse |
| Sound Theme | dropdown | `on_sound_theme_changed` | suoni attivi | forse |
| Output Device | dropdown | `on_audio_device_changed` | suoni attivi e `settings_audio_device_available` | forse |
| Preview Sounds | Sound preview overlay | `on_preview_sounds` | suoni attivi | no |
| Test Tracker | play/stop | `on_test_tracker` | suoni attivi | no |

### 9.3 Printer (Printer Manager, `printer_manager_overlay.xml`)

| Elemento | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Immagine | Printer Image overlay (Auto-Detect, immagini fornite, custom, import USB, anteprima) | `on_change_printer_image_clicked`, `on_printer_image_auto_detect` | `printer_image_usb_visible` | forse |
| Nome | Modifica in linea | `pm_printer_name_clicked` | `pm_name_editing` | si |
| Modello | Printer Model overlay (macro, ruoli ventole, calibrazione) | `on_change_printer_model_clicked` | - | si |
| Versioni | Klipper, Moonraker, HelixScreen | - | - | si |
| Chip capacita | Probe, Bed Mesh (apre), Heated Bed, LEDs (apre), ADXL (apre), QGL, Z-Tilt, Retraction (apre), Spoolman (apre), Timelapse (apre), Screws Tilt (apre), Webcam, Multi-Filament (apre), Fans (apre), Power (apre), Chamber, Speaker (apre), Nozzle Clean | `pm_chip_*_clicked` | ciascuno dal proprio `printer_has_*` / `ams_type` / `power_device_count` | si (diagnostica) |
| Manage Printers | Printer list overlay: "Show Printer Switcher", lista, delete, "+ Add Printer" | `pm_manage_printers_clicked`, `on_printer_switcher_changed`, `printer_list_row_cb`, `printer_list_delete_cb`, `printer_list_add_cb` | - | forse |

### 9.4 Printing (`settings_printing_overlay.xml`)

| Opzione | Valori / azione | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Toolhead Style | Auto / ... | `on_toolhead_style_changed` | - | forse |
| G-code Preview | Auto / 3D View / 2D Layers / Thumbnail Only | `on_gcode_mode_changed` | - | si |
| Z Movement | Auto / Bed Moves / Nozzle Moves | `on_z_movement_style_changed` | - | si: K2 muove il piatto |
| Machine Limits | Max Velocity, Max Acceleration, Accel to Decel, Square Corner Velocity, Extrude Speed (slider + campo), Max Z velocity/accel (sola lettura), Reset | `on_max_velocity_changed`, `on_max_accel_changed`, `on_accel_to_decel_changed`, `on_square_corner_velocity_changed`, `on_extrude_speed_changed`, `on_limit_field_clicked`, `on_limits_reset` | - | si |
| Retraction Settings | Enable, Retract Length/Speed, Unretract Extra/Speed | `on_retraction_enabled_changed`, `on_retraction_setting_changed`, `on_retraction_field_clicked` | `printer_has_firmware_retraction` | forse |
| Material Temperatures | Lista materiali, modifica Nozzle Min/Max, Bed, Preheat Macro, "Macro handles heating", Reset to Default, Save | `on_macro_dropdown_changed`, `on_material_reset_defaults`, `on_material_save` | `material_editing`, `material_has_macro` | si |
| Timelapse | Enable, Recording Mode (Layer/Hyperlapse), Framerate 15/24/30/60, Auto-render | `on_timelapse_enabled_changed`, `on_timelapse_mode_changed`, `on_timelapse_framerate_changed`, `on_timelapse_autorender_changed` | `printer_has_timelapse` | forse |
| Macro Buttons | Quick Button 1-4; standard: Load Filament, Unload Filament, Purge, Pause, Resume, Cancel, Bed Mesh, Bed Level, Clean Nozzle, Heat Soak | `on_quick_button_1..4_changed`, `on_load_filament_changed`, `on_unload_filament_changed`, `on_purge_changed`, `on_pause_changed`, `on_resume_changed`, `on_cancel_changed`, `on_bed_mesh_changed`, `on_bed_level_changed`, `on_clean_nozzle_changed`, `on_heat_soak_changed` | - | si: mappatura macro Kalico |

### 9.5 Hardware & Devices (`settings_hardware_overlay.xml`)

| Riga | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Hardware Health | Overlay: stato, CRITICAL / MISSING CONFIGURED / NEWLY DISCOVERED / CHANGED FROM LAST SESSION; per issue Ignore / Save | `on_hardware_health_clicked` | sezioni per conteggi | si |
| Printers | Printer list overlay | `on_printers_clicked` | - | forse |
| Camera | Vista camera | `on_camera_view_clicked` | `printer_has_webcam` e `platform_extras_available` | si |
| Multi-Filament System Management | Device Operations overlay (8.7) | `on_ams_settings_clicked` | `ams_slot_count`>0 | si |
| Fans | Fan settings: controllabili / auto, rinomina (`fan_rename_modal`) | `on_fans_settings_clicked`, `on_fan_rename_confirm/_cancel` | `fans_version`!=0 | si |
| Sensors | Switch sensors (master "Enable Monitoring", ruolo None/Runout/Toolhead/Entry, enable), probe, width (ruolo Flow Compensation), humidity, accelerometri, color (TD-1), temperatura (Chamber Heater / Chamber Sensor override) | `on_filament_sensors_clicked`, `on_switch_master_toggle_changed` | `filament_sensor_count`>0; sezioni per conteggi | si |
| LED Settings | LED selection (chip), LED on at Start + Startup Brightness, Automatic LED Control (righe per stato), Macro devices (+ Add) | `on_led_settings_clicked`, `on_led_on_at_start_changed`, `on_startup_brightness_changed/_commit`, `on_auto_state_changed`, `on_add_macro_device` | `printer_has_led` | si |
| Power Devices | Main power button chip, lista toggle, lock | `on_power_devices_clicked`, `on_power_device_toggle` | `power_device_count`>0 | forse |
| Spoolman | Server setup (host/port, Connect/Cancel), stato (Change/Remove), Sync + intervallo 30 s/1/2/5 min, Label Printer, Barcode Scanner | `on_spoolman_settings_clicked`, `on_spoolman_connect_clicked`, `on_spoolman_change_clicked`, `on_spoolman_remove_clicked`, `on_ams_spoolman_sync_toggled`, `on_ams_spoolman_interval_changed`, `on_spoolman_label_printer_clicked`, `on_barcode_scanner_clicked` | label/scanner se `platform_extras_available` | forse |
| Label Printer | Tipo, rete/USB/BT, indirizzo, dimensione, preset, Test Print | `on_lp_*` | - | no |
| Barcode Scanner | Layout tastiera, USB, BT scan/pair/forget | `on_bs_*` | `scanner_bt_available` | no |

### 9.6 Safety & Notifications (`settings_safety_overlay.xml`)

| Opzione | Valori | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| E-Stop Confirmation | toggle | `on_estop_confirm_changed` | - | si |
| Cancel Escalation | toggle | `on_cancel_escalation_changed` | - | si |
| Escalation Timeout | 15 / 30 / 60 / 120 s | `on_cancel_escalation_timeout_changed` | escalation attiva | si |
| Confirm before running macros | toggle | `on_macro_confirm_changed` | - | si |
| Confirm tool changes | toggle | `on_tool_change_confirm_changed` | `tool_count`>=2 | no |
| Allow cold load/unload | toggle | `on_allow_cold_extrude_changed` | - | si |
| Cool nozzle after filament ops | toggle | `on_filament_auto_cooldown_changed` | - | si |
| Print Completion Alert | Off / Notification / Alert | `on_completion_alert_changed` | - | si |
| On-screen Alerts | All / Warnings & errors / Errors only | `on_min_toast_severity_changed` | - | si |

### 9.7 System (`settings_system_overlay.xml`)

| Riga | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Security | Set PIN / Change PIN / Remove PIN / Auto-lock (`pin_entry_modal.xml`) | `on_security_clicked`, `on_set_pin_clicked`, `on_change_pin_clicked`, `on_remove_pin_clicked`, `on_auto_lock_changed`, `pin_modal_*` | change/remove/auto-lock se `lock_pin_set` | si |
| Network Settings | Wi-Fi on/off (2.4 GHz), SSID, IP, MAC, forget, stato non risponde; Ethernet; Test Network (modal); reti disponibili + refresh; Add Hidden Network; password Wi-Fi | `on_network_clicked`, `on_wlan_toggle_changed`, `on_network_settings_forget`, `on_test_network_clicked`, `on_refresh_clicked`, `on_add_other_clicked`, `on_network_item_clicked`, `on_wifi_password_connect/_cancel`, `on_hidden_connect_clicked` | `show_network_settings`; `wifi_hardware_available`, `wifi_enabled`, `wifi_connected`, `eth_connected` | si |
| Host | Change Printer Host (host, porta, Test Connection, Save) | `on_change_host_clicked`, `on_change_host_test/_save/_cancel` | Save se `change_host_validated` | si |
| Touch & Input | Touch Calibration; Show Touch Points; Scroll Engage Distance; Long Press Time; Allow Home Screen Editing; Scroll Guard | `on_touch_input_clicked`, `on_touch_calibration_clicked`, `on_debug_touches_changed`, `on_scroll_limit_changed`, `on_long_press_time_changed`, `on_home_edit_mode_changed`, `on_scroll_guard_changed` | calibrazione se `show_touch_calibration` | si |
| Touch Calibration overlay | Crosshair, Accept/Retry/Cancel, hold per annullare | `on_touch_cal_*` | `touch_cal_state` | forse |
| Plugins | Loaded/Disabled/Failed, Disable | `on_plugins_clicked`, `on_plugin_card_disable_clicked` | beta e `platform_extras_available` | no |
| Performance | CPU, memoria, throttle host; card MCU | `on_system_performance_clicked` | `perf_available` | forse |
| Share Usage Data | toggle telemetria | `on_telemetry_changed` | - | no |
| View Telemetry Data | Eventi in coda, Clear All Events | `on_telemetry_view_data`, `on_telemetry_clear_queue` | telemetria attiva | no |
| Log Level | Warn / Info / Debug / Trace | `on_log_level_changed` | - | si |
| Restart HelixScreen | Riavvia app | `on_restart_helix_settings_clicked` | - | si |
| Factory Reset | Modal conferma, reset e wizard | `on_factory_reset_clicked`, `on_factory_reset_confirm/_cancel` | - | si |

### 9.8 Help & About (`settings_help_overlay.xml`, `about_settings_overlay.xml`)

| Riga | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Replay Welcome Tour | Tour | `on_replay_tour_clicked` | - | no |
| Upload Debug Bundle | Consenso, log Klipper/Moonraker, nota, upload, codice | `on_debug_bundle_clicked`, `on_debug_bundle_*` | `debug_bundle_state` | forse |
| Discord Community / Documentation | QR/link | `on_discord_clicked`, `on_docs_clicked` | - | no |
| About: Printer Name / Current Version | tap | `on_about_printer_name_clicked`, `on_about_version_clicked` | - | si |
| About: Update Channel | Stable/Beta (/Dev) | `on_about_update_channel_changed` | `show_beta_features`, `show_update_settings` | si: canale del fork |
| About: Check for Updates / Install Update | Update notify / download | `on_about_check_updates_clicked`, `on_about_install_update_clicked` | `show_update_settings` | si |
| About: Software Updates gestiti / non disponibili | Info | `on_about_updates_unavailable_clicked` | `updates_firmware_managed`, `updates_unavailable` | forse |
| About: Klipper, Moonraker, OS, Host, Install Root, Config Dir, Logs, Cache Dir, Print Hours, licenze | Info | `on_about_print_hours_clicked` | alcune nascoste su Android | si |

**Conteggio sezione 9:** 7 + 24 + 6 + 8 + 11 + 9 + 12 + 8 = 85.

---

## 10. Advanced panel e overlay (`advanced_panel.xml`, `ui_panel_advanced.cpp`)

### 10.1 Righe e pulsanti

| Riga | Cosa fa | Trigger | Gate | Rilevante? |
|---|---|---|---|---|
| Macro Browser | Macro panel (10.2) | `on_advanced_macros` | `nav_buttons_enabled` (tutto il pannello) | si |
| G-code Console | Console (10.2) | `on_console_row_clicked` | - | si |
| Print History | History dashboard (10.2) | `on_history_row_clicked` | - | si |
| Spoolman / "Not configured" | Spoolman panel | `on_advanced_spoolman` | `printer_has_spoolman` | forse |
| Timelapse Videos | Video, Render Now | `on_timelapse_videos_clicked`, `on_timelapse_render_now` | `printer_has_timelapse`=1 | forse |
| Setup Timelapse | Install overlay (SSH, Check Again) | `on_timelapse_setup_clicked`, `on_timelapse_install_action` | timelapse assente e `printer_has_webcam` | forse |
| Uninstall HelixPrint Plugin | Rimuove plugin | `on_helix_plugin_uninstall_clicked` | beta e `helix_plugin_installed`=1 | no |
| Configure PRINT_START | Macro enhance wizard (Keep Required / Make Optional, Apply, backup) | `on_configure_print_start`, `on_macro_enhance_*` | beta | forse |
| Phase Tracking | toggle | `on_phase_tracking_changed` | beta e plugin | forse |
| Bed Leveling | Screws tilt | `on_screws_tilt_row_clicked` | `printer_has_screws_tilt` | no |
| Input Shaping | Input shaper panel | `on_input_shaper_row_clicked` | `printer_has_accelerometer` | si |
| Probe Management | Probe overlay | `on_probe_row_clicked` | `printer_has_probe` | si: Cartographer |
| Z-Offset Calibration | Z-offset panel | `on_zoffset_row_clicked` | `printer_has_probe` | si |
| Automatic Heater Control Calibration | PID/MPC panel | `on_pid_tuning_clicked` | - | si: MPC solo su Kalico |
| Shutdown / Reboot | Shutdown modal (Both/Printer/Screen) | `on_advanced_power_clicked`, `on_restart_split_clicked`, `on_shutdown_split_clicked`, `on_shutdown_printer_clicked`, `on_reboot_printer_clicked` | `platform_host_power_supported` | forse |
| E-Stop (M112) | Arresto | `advanced_estop_clicked` | - | si |
| Restart Klipper | RESTART | `advanced_restart_klipper_clicked` | - | si |
| Firmware Restart | FIRMWARE_RESTART | `advanced_firmware_restart_clicked` | - | si |

### 10.2 Overlay raggiunti

| Overlay | Contenuto e azioni | Trigger principali | Gate | Rilevante? |
|---|---|---|---|---|
| Macros (`macro_panel.xml`, `macro_card.xml`) | Lista macro; tap esegue (parametri `macro_param_modal`: Run/Cancel; conferma se impostata); long-press: modalita modifica visibilita con checkbox + Save | `on_macro_row_clicked`, `on_macro_card_long_press`, `on_macros_edit_save`, `macro_param_modal_run_cb` | `macro_row_count`, `macro_edit_mode` | si |
| Console (`console_panel.xml`) | Storico, input G-code, Clear, Settings (Hide Temperature Reports, Hide Firmware Noise), Send | `on_console_send_clicked`, `on_console_clear_clicked`, `on_console_settings_clicked`, `on_console_filter_*_changed` | `console_has_entries` | si |
| History dashboard | Filtri Day/Week/Month/Year/All, Total Prints, Success Rate, Print Time, Filament Used, grafico per tipo, trend, View Full History | `history_filter_*_clicked`, `history_view_full_clicked` | `history_has_jobs` | si |
| History list | Ricerca, filtro stato (All/Completed/Failed/Cancelled), ordinamento (Date newest/oldest, Duration, Filename), righe | `history_search_changed`, `history_filters_toggle`, `history_filter_status_changed`, `history_sort_changed` | `history_list_panel_state` | si |
| History detail | Miniatura, stato, start/end, durata, layer, temp, filamento, tipo, layer height; View Timelapse, Delete, Reprint | `history_detail_view_timelapse`, `history_detail_delete`, `history_detail_reprint` | `history_detail_has_timelapse`, `history_detail_can_reprint` | si |
| Spoolman panel | Ricerca, filtro location, Refresh, + Add (spool wizard 3 step: vendor/+New, filament/+New, peso/prezzo/lotto/note, Create); tap spool: menu Set Active / Edit (modal) / Print Label / Duplicate / Archive / Delete | `on_spoolman_*`, `spoolman_context_*_cb`, `on_wizard_*`, `spoolman_edit_*_cb` | `spoolman_panel_state` | forse |
| Input shaper | Configurazione attuale X/Y, Calibrate X / Y / All, misura (Abort), risultati con chip shaper, confronto, Save, Retry, Help | `input_shaper_calibrate_all_cb`, `_x_cb`, `_y_cb`, `_cancel_cb`, `_chip_x/y_N_cb`, `_save_cb`, `_retry_cb`, `_close_cb`, `_help_cb` | `printer_has_accelerometer`, `input_shaper_state` | si |
| Probe overlay | Nome/tipo/offset; pannello per tipo (BLTouch, Beacon, Cartographer: Scan Cal / Touch Cal, Eddy, Klicky); Accuracy (modal con EMERGENCY STOP), Z-Offset, Bed Mesh; configurazione X/Y offset, Samples, Speed, Retract, Tolerance (modal edit, salva e riavvia) | `on_probe_accuracy`, `on_zoffset_cal`, `on_bed_mesh`, `on_carto_scan_cal`, `on_carto_touch_cal`, `on_probe_cfg_*`, `on_probe_config_save` | `probe_config_loaded`, tipo probe | si: Cartographer |
| Z-Offset calibration | Vedi 6.2 | - | - | si |
| PID / MPC | Metodo MPC/PID (`cal_is_kalico`), heater Extruder/Bed, preset materiale, +/- temperatura, ventola parte, Fan Calibration Quick/Detailed/Thorough, potenza heater, avviso migrazione MPC; Start, Abort, Done, Retry; risultati Kp/Ki/Kd o parametri MPC | `on_pid_start`, `on_cal_method_mpc/_pid`, `on_pid_heater_extruder/_bed`, `on_pid_preset_*`, `on_pid_temp_up/_down`, `on_cal_fan_*`, `on_cal_wattage_*`, `on_pid_abort`, `on_pid_done`, `on_pid_retry` | `pid_cal_state`, `cal_show_pid_fan`, `cal_show_fan_config`, `cal_show_wattage`, `cal_needs_migration` | si |
| Screws tilt | Vedi 6.2 | - | - | no |
| Belt tension (`panel_belt_tension.xml`) | Misura frequenze path A/B, strobe fine-tune, lock A/B | `belt_tension_*_cb` | callback registrati ma **nessun punto di ingresso trovato** nel codice stock | forse |

**Conteggio sezione 10:** 18 righe + 12 overlay = 30.

---

## 11. Modali e dialog

| Modale | Contenuto / pulsanti | Trigger | Quando | Rilevante? |
|---|---|---|---|---|
| Runout guidance (`runout_guidance_modal.xml`) | Messaggio (o "Refill the spool, then Resume." se autofeed); Manual: Load, Unload, Purge; Close (idle) o Cancel Print / Resume Print | `wire_ok/quaternary/quinary/cancel/tertiary/senary_button`; Load usa `backend->change_tool`/`load_filament` o macro | runout; `runout_autofeed_capable`, `print_state_enum`=2, `runout_resume_blocked` | si |
| Print cancel confirm | "Stop Print?" | `print_cancel_confirm_modal.xml` | Cancel | si |
| Abort progress | Annullamento in corso + EMERGENCY STOP | `on_abort_emergency_stop` | escalation | si |
| Print completion | Statistiche + OK | `on_print_complete_ok` | fine stampa (impostazione Alert) | si |
| Klipper recovery | Titolo, codice, messaggio; Restart Klipper / Firmware Restart / Dismiss | `recovery_restart_klipper_clicked`, `recovery_firmware_restart_clicked`, `recovery_dismiss_clicked` | shutdown/errore Klippy; `recovery_can_restart`, `recovery_has_code` | si |
| E-stop confirmation | Cancel / STOP | vedi 1.3 | - | si |
| Update notify | Nuova versione, View Changelog, Ignore / Install | `on_update_notify_install/_ignore/_close`, `on_update_toggle_changelog` | aggiornamento disponibile | si |
| Update download | Conferma, download, verifica, installazione, completato, errore (Retry), riavvio | `on_update_download_start/_cancel/_dismiss` | `download_status` 1..7 | si |
| Crash report | Dettagli, QR, Dismiss / Send Report | `on_crash_report_dismiss`, `on_crash_report_send` | dopo crash | si |
| Debug bundle | Vedi 9.8 | - | - | forse |
| Action prompt (`action_prompt_modal.xml`) | Prompt Klipper `action:prompt` con pulsanti dinamici, footer, percorso fault AFC | `action_prompt_manager.cpp` | macro che usano prompt | si: macro Kalico con prompt |
| Exclude object | Conferma esclusione | `exclude_object_modal.xml` | Print Status | si |
| Save Z-Offset | Avviso restart | `on_z_offset_save/_cancel` | Controls | si |
| Restart required | Later / Now | `on_restart_later_clicked`, `on_restart_now_clicked` | impostazioni che richiedono riavvio | si |
| Factory reset | Conferma | `on_factory_reset_confirm/_cancel` | System | si |
| Shutdown / Reboot | Both / Printer / Screen | vedi 10.1 | - | forse |
| Generic dialog (`modal_dialog.xml`) | Info/Warning/Error, primario/annulla | `modal_confirm`/`modal_alert` | gate avvio stampa, conferme varie | si |
| Preflight check / Filament mapping | Vedi 3 | - | - | si |
| AMS loading error / recover state / buffer | Vedi 8.10 | - | - | si / no / forse |
| Plugin install | Vedi 3 | - | - | forse |
| Telemetry info | "What We Collect" | `telemetry_info_modal.xml` | wizard/settings | no |
| Info QR | Messaggio + QR + OK | `on_modal_ok_clicked` | link | no |
| PIN entry | Nuovo PIN / conferma | `pin_modal_*` | Security | si |
| Change host / Wi-Fi password / Hidden network / Network test | Vedi 9.7 | - | - | si |
| Spaghetti detection | Abort / Reduce Sensitivity / Resume | - | AI detection | no |
| Color picker | Presets / Custom HSV, hex, Select | `color_picker_*_cb`, `hex_input_*_cb` | modifica colore | si |
| Filament catalog picker / product edit | Scelta filamento, reset (preset), modifica prodotto | `catalog_picker_*_cb`, `filament_product_*_cb` | preset/slot | si |
| Macro param / Macro enhance | Vedi 10.2 | - | - | si / forse |
| Bed mesh calibrate / rename / save config | Vedi 6.2 | - | - | si |
| Probe accuracy / probe config edit | Vedi 10.2 | - | - | si |
| Fan rename / Theme save as / Job queue / Clog config / Camera config / IPP print / Temp graph config / Favorite macro config | Vedi sezioni relative | - | - | variabile |

**Conteggio sezione 11:** 31 voci.

---

## 12. Checklist per il redesign (funzioni da mantenere raggiungibili sul K2)

Navigazione e globali
- [ ] Home, Controls, Filament, Settings, Advanced raggiungibili; Controls/Filament disabilitati se Klippy non pronto.
- [ ] Accesso a Print Select (lista file) anche senza il widget stock Print Status.
- [ ] E-stop visibile quando `estop_visible`=1 (stampa e preparazione), con conferma opzionale.
- [ ] Toast e storico notifiche (badge contatore, Clear All).
- [ ] Lock screen con PIN ed e-stop anche da bloccato.
- [ ] Dim / sleep / screensaver con risveglio al tocco.
- [ ] Dialog Klipper recovery (Restart Klipper, Firmware Restart, Dismiss).
- [ ] Action prompt Klipper (`action:prompt`).
- [ ] Notifica e installazione aggiornamenti (canale del fork) e crash report.

Home
- [ ] Edit mode (long-press, sposta, ridimensiona, rimuovi, configura, aggiungi da catalogo, reset).
- [ ] Pagine: aggiungi, elimina (non la principale), naviga.
- [ ] Widget usati sul K2: stato stampa, temperature (ugello/piatto/camera), luce, ventole, CFS (`ams`), bypass, spool attiva, sensore filamento, macro preferite, console, motion, camera, notifiche, network, firmware restart.

Stampa
- [ ] Lista file: card/lista, ordinamento, cartelle, USB, elimina (anche con long-press).
- [ ] Dettaglio file: anteprima/viewer, metadati, stato storico, card bypass attivo.
- [ ] Mapping tool -> slot CFS e modal Filament Mapping; preflight "Check filament" (Remap / Cancel / Print Anyway).
- [ ] Opzioni pre-stampa (bed mesh, clean nozzle, timelapse, ecc. se presenti).
- [ ] Tutti i gate di avvio (stop command, peso insufficiente, bypass attivo, filamento nel toolhead, filamento assente, tool non risolti, mismatch colore/grado/materiale) con Start Anyway.
- [ ] Ripristino mapping dopo la stampa.
- [ ] Print Status: progresso, layer, tempi, ETA, filamento usato, M117, motivo pausa, overlay preparazione, esiti (tap per chiudere).
- [ ] Pausa/Riprendi, Cancel con conferma ed escalation, Reprint, Tune, Light, Timelapse.
- [ ] Exclude objects (mappa + lista + conferma).
- [ ] Temperature in stampa (tap -> grafico) e ventole (tap -> fan control).
- [ ] Stato cambio slot e tool corrente CFS in stampa.
- [ ] Print Tune: speed, flow, reset 100%, babystep Z con passi e hint.
- [ ] Completion modal e runout guidance (Load / Unload / Purge / Resume / Cancel Print).

Controls / Filament
- [ ] Posizione, homing All/X/Y/XY/Z, Motors Off, jog XY e Z (Fine/Coarse/Turbo).
- [ ] Temperature ugello/piatto/camera con preset e valore custom; grafico temperature.
- [ ] Ventola parte (slider) e fan control per tutte le ventole.
- [ ] Z-offset: vista, tune, Save Z-Offset con avviso restart.
- [ ] Bed mesh (probe, profili carica/rinomina/elimina, save config).
- [ ] Z calibration (paper test) e Z-Tilt se configurato.
- [ ] Luce e LED control overlay.
- [ ] Macro quick buttons 1-4.
- [ ] Filament: Load, Unload, Purge, Extrude/Retract 5/10/25 mm, Cool Down, preset materiale (tap e long-press per modificarli), avviso sicurezza.

CFS (Multi-Filament)
- [ ] Ingresso al pannello CFS (Filament > Manage, widget Home, tool corrente in stampa).
- [ ] Slot: tap -> menu con Load, Unload, Check slot (`_BOX_RFID_READ_SLOT`), Spool Info, Clear Spool, Select Spool / Scan QR (se Spoolman).
- [ ] Edit Slot: colore, catalogo vendor/tipo/prodotto, residuo e tara, Save (`_BOX_SLOT_SET`), Change Filament (Spoolman), Save to Spoolman, Remap Tool (verificare `BOX_MODIFY_TN` su Kalico).
- [ ] Sidebar: slot caricato, stato azione, step progress, Unload, Reset (`BOX_ERROR_CLEAR`), Check slots (`BOX_INFO_REFRESH`), Settings.
- [ ] Canvas percorso: tap tubo -> menu slot; tap toolhead -> cambio tool/menu; nodo spool esterno.
- [ ] Bypass: toggle (sidebar, widget, device ops), bloccato in stampa, unload automatico prima, ON = `T<external>`, OFF = `BOX_UNLOAD`; visibile solo con voce `external` nel payload Fork.
- [ ] Menu spool esterno: Spool Info, Select Spool, Scan QR, Clear Spool; editor spool esterno (slot -2) e lane extra per OrcaSlicer.
- [ ] Device Operations: Home, Recover, Abort, Bypass Mode, Auto-Heat info, Status.
- [ ] Azioni CFS oggi irraggiungibili nello stock da esporre nel redesign: Auto-refill / runout swap, Clog Detection, Unload After Print, Read RFID On Insert, Read RFID At Startup, Clean Nozzle, Refresh RFID, Communication Test.
- [ ] Stato auto-refill (endless status) e ambiente CFS (temperatura, umidita, Material Comfort, nota "No dryer").
- [ ] Loading error modal (Retry, chiusura con clear fault).
- [ ] Overview multi-unita se si concatenano piu CFS.

Settings
- [ ] Display & Sound: lingua, fuso, formato ora, animazioni, etichette widget, scroll buttons, bed mesh render, rotazione, dark mode, tema, luminosita, dim, sleep, screensaver, sleep in stampa, suoni.
- [ ] Printer: nome, immagine, modello, capacita, gestione stampanti.
- [ ] Printing: G-code preview, Z movement, machine limits, retraction, material temperatures, timelapse, macro buttons e macro standard.
- [ ] Hardware: hardware health, camera, gestione CFS, fans (rinomina), sensors (ruoli, override camera), LED settings, power devices, Spoolman.
- [ ] Safety: conferma e-stop, cancel escalation e timeout, conferma macro, cold load/unload, cooldown dopo ops, completion alert, soglia toast.
- [ ] System: Security/PIN, Network (Wi-Fi/Ethernet/test/hidden), Host, Touch & Input (calibrazione, touch points, scroll, long press, edit Home, scroll guard), Performance, Log level, Restart HelixScreen, Factory reset.
- [ ] Help & About: versione, canale aggiornamenti, check/install update, info sistema, debug bundle.

Advanced
- [ ] Macro browser (esecuzione con parametri, visibilita), console G-code (+ filtri), storico (dashboard, lista, dettaglio con Reprint/Delete).
- [ ] Input shaping, Probe (Cartographer Scan/Touch Cal, accuracy, config), Z-offset, PID/MPC.
- [ ] Spoolman e timelapse se usati.
- [ ] Shutdown/Reboot host, E-Stop M112, Restart Klipper, Firmware Restart.

---

## Note finali sui punti critici (bypass / CFS Fork)

1. `supports_bypass` sul CFS flat e vero solo con `macro_variant_ == Fork` (`api_version == 1`) **e** una voce `slots[]` con `"external": true` (`ams_backend_cfs.cpp`, convergenza in `handle_status_update`).
2. ON = `T<external_slot_index_>` (attended, fino a 30 s di attesa inserimento); OFF = `BOX_UNLOAD`; stato attivo = `loaded_slot` uguale all'indice external (`current_slot == -2`).
3. Il load diretto dello slot -2 con Fork fallisce (`invalid_slot`): l'unica via e il toggle bypass.
4. Il toggle e sempre rifiutato durante un job; se una bay e caricata fa prima unload (`allows_implicit_chaining` base = true).
5. La riga spool esterno del Filament panel e nascosta quando c'e un AMS: sul K2 lo spool esterno si gestisce solo dal nodo bypass del pannello CFS, dal widget Active Spool o dal widget Bypass.
6. Le azioni device del CFS (incluse le 4 switch Fork e l'auto-refill) non sono raggiungibili nella UI stock perche `AmsBackendCfs` non definisce `get_device_sections()`.
7. Da verificare su Kalico: `BOX_ERROR_RESUME_PROCESS` (Recover) e `BOX_MODIFY_TN` (remap) sono emessi anche sotto Fork senza un ramo dedicato.

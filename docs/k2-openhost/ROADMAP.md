# HelixScreen K2-OpenHost: roadmap

Aggiornata il 28/10/2026. Fork: [MzTechnology97/helixscreen](https://github.com/MzTechnology97/helixscreen), ramo `k2-openhost`, basato su v1.0.6 (la versione installata sul T113).

## Regole del fork

- **Poche modifiche, isolate.** Ogni modifica è legata al dialetto Fork (formato `slots[]` con `api_version` 1, cioè Kalico). La CFS di fabbrica e le altre stampanti non cambiano.
- **Prima verso upstream.** Ogni correzione utile anche agli altri si propone a prestonbrown/helixscreen, ma solo dopo la tua approvazione del testo. Meno differenze teniamo, meno costa restare allineati: upstream pubblica una beta ogni 2–3 giorni.
- **Allineamento settimanale.** Il fork entra nel report settimanale che già usiamo per Kalico. Si aggiorna su ogni versione stabile (1.0.x, poi 1.1), non sulle beta.
- **Una sola fonte dei dati.** Per la CFS la fonte è Kalico. HelixScreen mostra e invia comandi, ma non tiene copie proprie dei dati.
- **Ogni correzione ha il suo test** nella CI del fork (test `[cfs]` e `[k2oh]`) e si prova sulla stampante prima del merge.
- **Versioni del fork:** `VERSION.txt` resta quello di upstream, `VERSION-k2oh.txt` numera le nostre build (`1.0.6-k2oh.1`). Per pubblicare una build si aumenta quel numero: la CI crea la release e l'aggiornamento automatico di HelixScreen la installa.

## Fase 0 (subito)

| # | Cosa | Stato |
| --- | --- | --- |
| 0.1 | CI del fork: pacchetto K2 più test `[cfs]` | fatto; il job dei test arriva con la PR #1 |
| 0.2 | Niente `lane_data` scritto da HelixScreen con Kalico (colore grigio dopo il reinserimento) | PR #1, test aggiornato, in attesa della CI |
| 0.3 | Installazione della nostra build sul T113: release nel fork, script nel bootstrap con backup e ripristino | pronta: `k2oh-setup helix-update` / `helix-restore` (bootstrap PR #17), release dalla PR #2; si installa a stampante ferma |
| 0.4 | Spegnere `ustreamer` sul T113: in OpenHost la telecamera è sul CM5 e `/dev/video0` sul T113 non esiste. Oggi gira a vuoto da 13 giorni (oltre 2 h di CPU, circa 5% fisso, `killall cam_app` ogni 5 s) | fatto il 28/10 a mano e nel bootstrap (PR #16); nel fork l'installer salta la telecamera se manca `/dev/video0` (PR #2) |
| 0.5 | Misure di riferimento durante una stampa: CPU per thread, frame `box` al secondo, tempo di parse | da fare |
| 0.6 | Versione propria (`1.0.6-k2oh.N`) e aggiornamenti automatici solo dalle release del fork; l'attesa all'avvio legge l'host Moonraker da `settings.json`, così un aggiornamento non perde il CM5 | PR #2 |

## Fase 1: dati CFS corretti sullo schermo

| # | Problema | Causa trovata | Correzione |
| --- | --- | --- | --- |
| 1.1 | Colore grigio dopo il reinserimento | auto-mirror `FillUnsetOnly` in `lane_data` | PR #1 |
| 1.2 | Utensili (T0–T3) diversi dalla mappatura reale | con il formato `slots[]` HelixScreen ignora `map` e `print_mapping` e usa sempre T*n* = slot *n* | PR #3: in stampa `print_mapping.map` (Kalico la azzera a fine stampa), altrimenti `box.map`; l'utensile corrente è quello che passa per lo slot caricato |
| 1.3 | Slot caricato in testa mostrato male | lo slot caricato era giusto, l'etichetta dell'utensile no (T3 invece di T0): è la 1.2 | da ricontrollare sullo schermo dopo l'installazione della PR #3 |
| 1.4 | Residuo del filamento non mostrato | il parse flat non legge i campi del residuo | PR #3: metri residui come testo, `rfid_percent` per la barra; niente grammi stimati (finirebbero in Spoolman) |
| 1.5 | Più CFS: temperatura e umidità solo della CFS che alimenta | gli slot sono già divisi per CFS; l'ambiente usa un solo valore | leggere `boxes[]`, una lettura per unità |
| 1.6 | Etichetta "sconosciuto" per tag non riconosciuti | campi `rfid_unknown_code` e `rfid_unknown_color` ignorati | mostrarli e proporre di creare il profilo |

## Fase 2: libreria filamenti nativa

**Oggi:** il servizio `k2oh-helix-filaments` sul T113 sostituisce il catalogo di HelixScreen (`filaments.json`) con la libreria di Kalico. Funziona, ma:
- serve un servizio Python sempre attivo;
- sostituisce un file del pacchetto, che ogni aggiornamento di HelixScreen riscrive;
- dopo ogni modifica serve un riavvio di HelixScreen, perché la cache dei codici RFID si ricostruisce solo all'avvio;
- i filamenti creati dallo schermo seguono un giro indiretto (`HXnnnn` tramite `_BOX_FILAMENT_SET`, solo a stampante ferma).

**Con il fork:** HelixScreen riceve già l'intera libreria in `box.filaments` (43 KB). Il catalogo può leggerla da lì quando parla con Kalico:

| | Oggi | Con il fork |
| --- | --- | --- |
| Fonte | copia in `filaments.json` | `box.filaments` dal vivo |
| Aggiornamento | riavvio di HelixScreen | immediato |
| Nuovo filamento dallo schermo | servizio esterno, `HXnnnn` | `_BOX_FILAMENT_SET` diretto |
| Preset Orca | non mostrato | nome del preset accanto al profilo |
| Aggiornamenti di HelixScreen | possono cancellare la libreria | nessun file da sostituire |
| Servizi sul T113 | `k2oh-helix-filaments` | nessuno, servizio rimosso |

Passi:
1. Il backend del catalogo legge `box.filaments`, solo con il dialetto Fork.
2. Ricerca per codice RFID dagli stessi dati.
3. Creazione e modifica con i comandi di Kalico.
4. Rimozione del servizio dal bootstrap, con migrazione.

Questa fase resta nostra: upstream non ha un'altra stampante con una libreria lato host.

## Fase 3: fluidità e reattività

Situazione misurata (28/10, a riposo):
- T113: 2 core e 488 MB di RAM. HelixScreen usa 29 MB e il 5% di CPU.
- Durante la stampa, invece, il processo precedente ha accumulato 11 h 37 min di CPU in circa 22 h.
- Il binario è già ottimizzato (`-O2`, NEON, LTO, statico) e le animazioni sono già spente.

Dove si perde tempo, in ordine di guadagno atteso:

1. **Troppi dati da Kalico.**
   - HelixScreen si iscrive a tutto `box` (77 KB, di cui 25 KB di `orca_presets` mai usati).
   - Durante la stampa cambiano di continuo il residuo stimato, l'encoder e l'ambiente, quindi arriva un frame fino a 4 volte al secondo.
   - Interventi:
     - iscriversi solo ai campi usati;
     - lato Kalico, arrotondare o limitare i campi che cambiano spesso (residuo a 0,1 m, ambiente a 1 °C / 1 %).
2. **Elaborazione completa a ogni frame.**
   - Con il formato flat ogni frame conta come "aggiornamento completo": unione dello stato, parse di tutti gli slot, override, poi evento di aggiornamento dell'intera interfaccia.
   - Intervento: rifare il parse solo se sono cambiati campi che contano (`slots`, `loaded_slot`, `map` e simili) e aggiornare solo la parte di interfaccia interessata.
3. **Indicizzazione del G-code all'avvio della stampa.**
   - Circa 33 s per 15 M di righe, durante i quali il touch risponde a fatica.
   - Interventi: priorità più bassa o in background a blocchi; oppure saltarla, perché il CM5 ha già i metadati (miniatura, tempi) tramite Moonraker.
4. **Telecamera.**
   - Spegnere `ustreamer` sul T113 (0.4, fatto).
   - Puntare la vista telecamera di HelixScreen al flusso del CM5 (crowsnest).
5. **Conversione dei colori.**
   - LVGL disegna in RGB565 ma lo schermo è XRGB8888 a 32 bit, quindi c'è una conversione a ogni aggiornamento.
   - Da misurare prima: un framebuffer a 16 bit dipende dal driver del kernel.
6. **Rete.**
   - Moonraker passa dal Wi-Fi a 2,4 GHz del T113.
   - Il risparmio energetico è già spento. Le disconnessioni sono da confrontare con le 59 in 12 giorni misurate prima delle correzioni.

Ogni intervento si misura prima e dopo con gli stessi numeri della fase 0.5.

## Fase 4: funzioni utili a K2-OpenHost

In ordine di utilità; ognuna usa dati che Kalico già pubblica.

1. **Rilettura RFID di uno slot** dal menu dello slot, utile per le bobine Bambu (`BOX_RFID_BAMBU_READ`).
2. **Esaurimento e cambio automatico:** ordine di cambio, gruppi di bobine identiche (`runout_rank`, `runout_groups`, `runout_order`).
3. **Umidità:** avviso di essiccazione per slot (`humidity_pct`, `humidity_limit_pct`).
4. **Parametri per slot** nell'editor: pressure advance e flusso massimo con la loro fonte (`pressure_advance`, `max_flow`).
5. **Stato operazione e recupero:** cosa sta facendo la CFS e come riprendere dopo un errore (`operation`, `recovery`).
6. **Interruttori:** scarico a fine stampa, rilevamento intasamenti, cambio automatico (`unload_after_print_enabled`, `clog_detection_enabled`, `runout_swap_enabled`).
7. **Mappatura all'avvio della stampa dallo schermo** con le proposte di Kalico (`print_mapping`).
8. **Widget CFS per la Home:** le bobine di ogni CFS con colore, materiale e percentuale residua, lo slot in uso con il suo utensile, temperatura e umidità della CFS; un tocco apre il pannello Multi-filamento. Si parte dal widget "Multi-filamento" che HelixScreen ha già (`panel_widget_ams`, `ams_mini_status`) e si usano i dati della CFS (`slots[]`, `rfid_percent`, `boxes[]`, `print_mapping`).

## Rischi

- **Il fork resta indietro rispetto a upstream:** modifiche piccole e un report settimanale. Le correzioni generali vanno proposte a upstream.
- **Compilazione lunga:** la prima senza cache supera l'ora. La cache del fork accorcia le successive.
- **Installazione sul T113:** va sempre con backup e `--restore`, come gli altri servizi del bootstrap.
- **Il residuo dipende dal firmware della CFS:** con la CFG4 vale solo la stima di Kalico (`rfid_remaining_source: kalico`).

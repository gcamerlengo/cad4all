# CAD4All

CAD 2D e 3D parametrico per tecnici, open source. Il disegno 2D funziona come AutoCAD (comandi, alias, riga di comando, snap); il 3D parametrico in stile ArchiCAD arriverà dalle prossime versioni.

![CAD4All 0.1](docs/immagini/cad4all-0.1.png)

## Stato

Versione 0.1, nucleo 2D. Funziona già:

- **Disegno:** linea, polilinea con tratti dritti e archi, cerchio (raggio o diametro), arco per tre punti, rettangolo, tratteggio (contorno trovato cliccando un punto interno, con le isole, o scegliendo oggetti chiusi; 17 motivi ISO tra cui ANSI31, NET, BRICK, EARTH e il pieno SOLID).
- **Modifica:** cancella, sposta, copia (multipla), ruota, scala, specchio, offset (a distanza o per punto), taglia, estendi, raccordo tra linee, archi e cerchi (con raggio o a spigolo vivo), cima tra linee (due distanze o lunghezza e angolo), raccordo e cima su tutti gli angoli di una polilinea, stira con finestra interseca.
- **Annotazioni:** testo su una o più righe (allineato a sinistra, al centro o a destra, con rotazione), quote lineari (orizzontali, verticali, ruotate), allineate, di raggio e di diametro, con testo personalizzabile e scala delle quote.
- **Precisione:** coordinate assolute e relative (`10,20`, `@5,0`, `@10<45`), inserimento diretto della distanza, snap ad oggetto (fine, punto medio, centro, quadrante, intersezione, inserimento), orto.
- **Selezione:** clic, finestra (da sinistra a destra) e interseca (da destra a sinistra), Maiusc+clic per togliere.
- **Organizzazione:** layer con colore, visibilità e blocco.
- **File:** formato nativo `.c4a` (JSON leggibile), apertura dei DWG di AutoCAD, apertura e salvataggio DXF, stampa su stampante e in PDF in scala, annulla e ripristina illimitati, script di comandi `.scr`.

In arrivo: muri, porte e finestre parametrici in 3D.

## Comandi

Si scrivono sulla riga di comando, in italiano, in inglese o con l'alias. Invio o Spazio confermano, Esc annulla, Invio a vuoto ripete l'ultimo comando. Il tasto destro del mouse vale come Invio.

| Comando | Alias | Opzioni |
| --- | --- | --- |
| LINEA / LINE | L | Annulla, Chiudi |
| PLINEA / PLINE | PL | Arco (archi tangenti), Linea, Chiudi, ANnulla |
| CERCHIO / CIRCLE | C | Diametro |
| ARCO / ARC | A | tre punti |
| RETTANGOLO / RECTANG | REC | |
| TRATTEGGIO / HATCH | H, BH | punto interno o Oggetti, Motivo (? per l'elenco), Scala, ANgolo; Invio crea il tratteggio |
| MODIFICATRATTEGGIO / HATCHEDIT | HE | Motivo, Scala, ANgolo di un tratteggio esistente |
| CANCELLA / ERASE | E | (anche tasto Canc) |
| SPOSTA / MOVE | M | |
| COPIA / COPY | CO, CP | Esci |
| RUOTA / ROTATE | RO | angolo in gradi o punto |
| SCALA / SCALE | SC | fattore o punto |
| SPECCHIO / MIRROR | MI | Cancellare oggetti sorgente? Sì/No |
| OFFSET | O | distanza o Punto, Esci |
| TAGLIA / TRIM | TR | clic sulla parte da togliere, Esci |
| ESTENDI / EXTEND | EX | clic vicino all'estremità da allungare, Esci |
| RACCORDO / FILLET | F | Raggio (0 = spigolo vivo), Polilinea (tutti gli angoli); tra linee, archi e cerchi |
| CIMA / CHAMFER | CHA | Distanza, Angolo, Polilinea (tutti gli angoli) |
| STIRA / STRETCH | S | finestra interseca, poi punto base e secondo punto |
| TESTO / TEXT | DT | Giustifica (Sinistra/Centro/Destra); Spazio scrive uno spazio, Invio va a capo, Invio a vuoto termina |
| MODIFICATESTO / TEXTEDIT | ED | cambia un testo o il testo di una quota (`<>` = valore misurato) |
| QUOTALIN / DIMLINEAR | DLI | Invio per selezionare una linea; Testo, Orizzontale, Verticale, Ruotato |
| QUOTALLINEATA / DIMALIGNED | DAL | Invio per selezionare una linea; Testo |
| QUOTARAGGIO / DIMRADIUS | DRA | Testo |
| QUOTADIAMETRO / DIMDIAMETER | DDI | Testo |
| SCALAQUOTE / DIMSCALE | | moltiplica le dimensioni delle nuove quote (testo e frecce 2,5) |
| ZOOM | Z | Estensioni |
| LAYER | LA | apre il pannello layer |
| STAMPA / PLOT | | apre la finestra di stampa (anche Ctrl+P) |
| ANNULLA / UNDO | U | (anche Ctrl+Z) |
| RIPRISTINA / REDO | | (anche Ctrl+Y) |

Tasti: F3 snap ad oggetto, F7 griglia, F8 orto. Rotella per lo zoom, tasto centrale per spostare la vista, doppio clic del tasto centrale per lo zoom estensioni.

## Compilare

Servono CMake 3.28 o successivo, un compilatore C e C++20, Qt 6.4 o successivo e git. Alla prima configurazione CMake scarica e compila LibreDWG per la lettura dei DWG (un paio di minuti); per farne a meno: `-DCAD4ALL_WITH_DWG=OFF`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/app/CAD4All
```

Su Ubuntu 24.04 le dipendenze si installano con `sudo apt install cmake ninja-build qt6-base-dev libgtest-dev`. Su Windows e macOS basta l'installer ufficiale di Qt. Ad ogni modifica GitHub Actions compila e prova il programma sui tre sistemi e conserva i pacchetti di Windows e macOS.

Per provare uno script: `CAD4All --script docs/esempi/pianta.scr` oppure `docs/esempi/modifica.scr` `docs/esempi/quote.scr`, `docs/esempi/cima-stira.scr`, `docs/esempi/polilinee.scr` e `docs/esempi/tratteggi.scr`.

![Testi e quote](docs/immagini/testi-quote.png)

## DWG e DXF

File > Apri legge i DWG di AutoCAD dalla versione R14 alla 2018 e successive grazie a [LibreDWG](https://www.gnu.org/software/libredwg/) (GPL 3), con gli stessi oggetti del DXF. Il DWG non viene mai sovrascritto: Salva chiede dove salvare il disegno in `.c4a` o `.dxf`.


File > Apri legge i DXF di qualsiasi versione salvati da AutoCAD o da altri CAD: linee, cerchi, archi, polilinee (anche con archi), testi e testi multilinea, quote, tratteggi (anche con motivi che CAD4All non ha), ellissi e spline (convertite in polilinee), solidi, direttrici e blocchi (esplosi, con i loro attributi), con i layer e i loro colori. Quello che non viene ancora letto (per esempio i solidi 3D) è elencato sulla riga di comando dopo l'apertura.

File > Salva con estensione `.dxf` o File > Esporta DXF scrivono un DXF R12, che si apre in ogni CAD; le quote restano quote, i tratteggi diventano blocchi di linee come in AutoCAD R12 (si vedono uguali ma non si modificano più come tratteggi). Da riga di comando: `CAD4All --script disegno.scr --export disegno.dxf`, oppure `CAD4All disegno.dwg --export disegno.dxf` per convertire un DWG.

![Polilinee con archi, raccordi e cime](docs/immagini/polilinee.png)

## Tratteggi

TRATTEGGIO (alias H) chiede un punto interno: il contorno chiuso più piccolo attorno al punto viene trovato da solo, anche se è fatto di linee separate che si incrociano, e gli oggetti chiusi al suo interno (un tavolo, un pilastro) restano vuoti. Si possono cliccare più zone, Invio crea il tratteggio. I motivi hanno le misure di AutoCAD in millimetri (`acadiso.pat`): per un disegno in centimetri o in metri la Scala va aumentata; se il motivo è troppo fitto CAD4All lo dice e suggerisce una scala.

![Tratteggi](docs/immagini/tratteggi.png)

## Stampa e PDF

File > Stampa (Ctrl+P, o il comando STAMPA) stampa su una stampante o salva un PDF: formato da A4 ad A0, orientamento, estensioni o vista corrente, scala adattata al foglio o 1:50, 1:100 e così via con l'unità del disegno, spessore delle linee e bianco e nero, con l'anteprima del foglio. Da riga di comando `--export disegno.pdf` salva un PDF A3 in bianco e nero adattato al foglio.

## Struttura

| Cartella | Contenuto |
| --- | --- |
| `src/core` | Geometria, entità e documento con annulla; C++ puro, senza Qt |
| `src/io` | Lettura e scrittura dei file (formato nativo, DXF, DWG con LibreDWG) |
| `src/engine` | Motore dei comandi e comandi stile AutoCAD (usa solo QtCore, si prova senza finestra) |
| `src/app` | Interfaccia Qt: vista di disegno, riga di comando, pannello layer, stampa |
| `tests` | Test automatici (GoogleTest) |

## Licenza

CAD4All è software libero: puoi ridistribuirlo e modificarlo secondo i termini della GNU General Public License, versione 3 o successiva. Il testo completo è nel file [LICENSE](LICENSE).

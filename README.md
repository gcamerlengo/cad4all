# CAD4All

CAD 2D e 3D parametrico per tecnici, open source. Il disegno 2D funziona come AutoCAD (comandi, alias, riga di comando, snap); il 3D parametrico in stile ArchiCAD arriverà dalle prossime versioni.

![CAD4All 0.1](docs/immagini/cad4all-0.1.png)

## Stato

Versione 0.1, nucleo 2D. Funziona già:

- **Disegno:** linea, polilinea, cerchio (raggio o diametro), arco per tre punti, rettangolo.
- **Modifica:** cancella, sposta, copia (multipla), ruota, scala, specchio, offset (a distanza o per punto), taglia, estendi, raccordo tra linee, archi e cerchi (con raggio o a spigolo vivo), cima tra linee (due distanze o lunghezza e angolo), stira con finestra interseca.
- **Annotazioni:** testo su una o più righe (allineato a sinistra, al centro o a destra, con rotazione), quote lineari (orizzontali, verticali, ruotate), allineate, di raggio e di diametro, con testo personalizzabile e scala delle quote.
- **Precisione:** coordinate assolute e relative (`10,20`, `@5,0`, `@10<45`), inserimento diretto della distanza, snap ad oggetto (fine, punto medio, centro, quadrante, intersezione, inserimento), orto.
- **Selezione:** clic, finestra (da sinistra a destra) e interseca (da destra a sinistra), Maiusc+clic per togliere.
- **Organizzazione:** layer con colore, visibilità e blocco.
- **File:** formato nativo `.c4a` (JSON leggibile), annulla e ripristina illimitati, script di comandi `.scr`.

In arrivo: raccordo e cima sulle polilinee, tratteggi, import ed export DXF, lettura DWG con LibreDWG, stampa PDF, poi muri, porte e finestre parametrici in 3D.

## Comandi

Si scrivono sulla riga di comando, in italiano, in inglese o con l'alias. Invio o Spazio confermano, Esc annulla, Invio a vuoto ripete l'ultimo comando. Il tasto destro del mouse vale come Invio.

| Comando | Alias | Opzioni |
| --- | --- | --- |
| LINEA / LINE | L | Annulla, Chiudi |
| PLINEA / PLINE | PL | Annulla, Chiudi |
| CERCHIO / CIRCLE | C | Diametro |
| ARCO / ARC | A | tre punti |
| RETTANGOLO / RECTANG | REC | |
| CANCELLA / ERASE | E | (anche tasto Canc) |
| SPOSTA / MOVE | M | |
| COPIA / COPY | CO, CP | Esci |
| RUOTA / ROTATE | RO | angolo in gradi o punto |
| SCALA / SCALE | SC | fattore o punto |
| SPECCHIO / MIRROR | MI | Cancellare oggetti sorgente? Sì/No |
| OFFSET | O | distanza o Punto, Esci |
| TAGLIA / TRIM | TR | clic sulla parte da togliere, Esci |
| ESTENDI / EXTEND | EX | clic vicino all'estremità da allungare, Esci |
| RACCORDO / FILLET | F | Raggio (0 = spigolo vivo); tra linee, archi e cerchi |
| CIMA / CHAMFER | CHA | Distanza, Angolo |
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
| ANNULLA / UNDO | U | (anche Ctrl+Z) |
| RIPRISTINA / REDO | | (anche Ctrl+Y) |

Tasti: F3 snap ad oggetto, F7 griglia, F8 orto. Rotella per lo zoom, tasto centrale per spostare la vista, doppio clic del tasto centrale per lo zoom estensioni.

## Compilare

Servono CMake 3.21 o successivo, un compilatore C++20 e Qt 6.4 o successivo.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/app/CAD4All
```

Su Ubuntu 24.04 le dipendenze si installano con `sudo apt install cmake ninja-build qt6-base-dev libgtest-dev`. Su Windows e macOS basta l'installer ufficiale di Qt. Ad ogni modifica GitHub Actions compila e prova il programma sui tre sistemi e conserva i pacchetti di Windows e macOS.

Per provare uno script: `CAD4All --script docs/esempi/pianta.scr` oppure `docs/esempi/modifica.scr` `docs/esempi/quote.scr` e `docs/esempi/cima-stira.scr`.

![Testi e quote](docs/immagini/testi-quote.png)

## Struttura

| Cartella | Contenuto |
| --- | --- |
| `src/core` | Geometria, entità e documento con annulla; C++ puro, senza Qt |
| `src/io` | Lettura e scrittura dei file |
| `src/engine` | Motore dei comandi e comandi stile AutoCAD (usa solo QtCore, si prova senza finestra) |
| `src/app` | Interfaccia Qt: vista di disegno, riga di comando, pannello layer |
| `tests` | Test automatici (GoogleTest) |

## Licenza

CAD4All è software libero: puoi ridistribuirlo e modificarlo secondo i termini della GNU General Public License, versione 3 o successiva. Il testo completo è nel file [LICENSE](LICENSE).

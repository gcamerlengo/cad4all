# Guida per chi prova CAD4All

Grazie per provare CAD4All. È una versione di prova: alcuni comandi di AutoCAD e ArchiCAD mancano ancora e qualcosa può non funzionare. Le sue segnalazioni decidono che cosa viene fatto prima.

## Installare

Scarichi l'installatore per il suo sistema dalla pagina [Releases](https://github.com/gcamerlengo/cad4all/releases) (sezione *Assets* dell'ultima versione).

Gli installatori non sono ancora firmati digitalmente, quindi il sistema mostra un avviso la prima volta. È normale.

- **Windows 10 e 11:** apra `CAD4All-<versione>-Windows-setup.exe`. Se compare "PC protetto da Windows", clicchi *Ulteriori informazioni* e poi *Esegui comunque*. Se non vuole installare nulla, la cartella `portatile-Windows` parte senza installazione.
- **macOS:** apra `CAD4All-<versione>-macOS.dmg` e trascini CAD4All in Applicazioni. Alla prima apertura macOS dice che non può verificare lo sviluppatore: chiuda l'avviso, apra *Impostazioni di Sistema > Privacy e sicurezza*, scorra in basso e clicchi *Apri comunque* accanto a CAD4All. Sulle versioni di macOS precedenti alla 15 basta il tasto destro su CAD4All > *Apri*.
- **Linux:** scarichi `CAD4All-<versione>-Linux-x86_64.AppImage`, lo renda eseguibile (`chmod +x CAD4All-*.AppImage`, oppure Proprietà > Permessi > Eseguibile) e lo avvii. Se non parte, installi il pacchetto `libfuse2` (`libfuse2t64` su Ubuntu 24.04).

## Primi passi

- In basso c'è la riga di comando, come in AutoCAD: si scrive il comando (in italiano o in inglese, per esempio `LINEA` o `L`) e si preme Invio. `?` elenca tutti i comandi.
- *Visualizza > Interfaccia* passa dall'interfaccia simile ad AutoCAD (barra multifunzione) a quella simile ad ArchiCAD (Strumenti, Info Box, Navigatore).
- Muri, porte, finestre, solai, tetti e scale sono nel menu *Architettura*; la vista 3D si aggiorna mentre si disegna.
- Si possono aprire disegni DWG e DXF esistenti e salvare in `.c4a` (formato di CAD4All) o DXF. Il DWG per ora si apre soltanto.

## Cosa provare

Lo usi per un lavoro vero, anche piccolo: un rilievo, una pianta, una modifica a un DWG di un cliente. Ci interessa soprattutto:

1. che cosa le manca per finire il lavoro senza tornare ad AutoCAD o ArchiCAD;
2. i DWG e DXF che non si aprono o si aprono male;
3. i comandi che si comportano in modo diverso da come si aspetta;
4. qualsiasi chiusura improvvisa del programma.

## Salvataggio automatico

Ogni 5 minuti, se il disegno ha modifiche, CAD4All ne salva una copia di sicurezza. Se il programma si chiude all'improvviso, alla riapertura propone di recuperare il disegno. Salvi comunque spesso con Ctrl+S.

## Segnalare un problema o un suggerimento

Da CAD4All: *Aiuto > Segnala un problema...* apre la pagina della segnalazione con la versione e il sistema già scritti. Serve un account GitHub (gratuito).

Nella segnalazione indichi:

- che cosa ha fatto (i comandi, in ordine) e che cosa è successo;
- il disegno, se possibile (trascinandolo nella pagina, anche in `.zip`);
- uno screenshot.

Per un comando che manca scelga *Suggerisci una funzione*. Se non usa GitHub, scriva a g.camerlengo@gmail.com con le stesse informazioni.

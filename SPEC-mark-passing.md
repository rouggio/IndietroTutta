# Passaggio boa — LEGGE (bloccata)

Ratificata dal velista il 2026-10-17. Funziona: 27 passaggi su 27 buoni su
tutti i corsi, figure in `../IndietroTuttaBackend/out/sim-routes/`.

NON TOCCARE. Non reinterpretare, non "migliorare", non derivarne varianti.
Si cambia solo su ordine esplicito del velista, e cambiando insieme:
questa legge, la prova offline (`store/simShape.js` + figure) e il motore
(`src/race_run.cpp`). Una delle tre da sola non si tocca mai.

## 1. Il percorso

Una lista in ordine: segmento di partenza, boe, arrivo. Ogni boa ha un lato
dichiarato: **P** o **S**. Le porte sono coppie di boe. Per valutare una boa
servono tre punti: **A** (da dove vieni), **B** (la boa), **C** (dove vai dopo).

## 2. I due traversi

- **Traverso d'entrata**: passa da B ed è perpendicolare all'andata A→B.
- **Traverso d'uscita**: passa da B ed è perpendicolare alla rotta B→C.
- Su ogni traverso c'è una destra e una sinistra guardando nel verso della
  rotta (andata o uscita). Tagliare a **destra** dell'asse = la barca passa a
  destra della boa = gira **CCW** intorno alla boa = **P**; a sinistra = **CW** = **S**.

## 3. Il passaggio buono

1. **Aggancio**: la barca taglia il traverso d'entrata allontanandosi da A —
   sta davvero arrivando alla boa, non manovrando lì intorno.
2. **Giro**: gira intorno a B in CCW (P) o CW (S) verso C. Si arriva e si
   prosegue: mai arrivare vicino alla boa per poi tornare bruscamente indietro.
3. **Uscita**: taglia il traverso d'uscita andando verso C, allontanandosi da B.
   In quello stesso istante: punteggio, cambio bersaglio, uscita zoom — se il
   taglio è dal lato dichiarato.

Se dopo l'aggancio la barca torna verso A, l'aggancio si cancella: chi arriva
vicino e torna indietro non conta niente.

**Ampiezza del giro — ratificato dal velista il 2026-10-20.** Il giro non ha
limite di ampiezza. Con la boa dichiarata **P** su un lato che piega a dritta
(`Easy Peasy`: +119° al primo lato) la barca gira **241° in CCW** e tiene la boa
a sinistra: è un giro, non un ritorno. Poiché i traversi sono **rette** (2), un
giro oltre i 180° attraversa il traverso d'entrata una seconda volta *per
costruzione* — quell'attraversamento, da solo, **non** cancella l'aggancio. La
cancellazione vale solo se dall'aggancio **non c'è stato giro nel verso
dichiarato**, cioè se la barca si è avvicinata e ha invertito. La direzione si
legge su più punti (dettaglio ammesso da 6): segno della rotazione del
rilevamento barca→B, tre oscillazioni consecutive oltre il rumore (2°). La prova
offline (`store/simShape.js`, colonna `entryLost`) applica la stessa regola, e
una rotta che gira dalla parte sbagliata resta bocciata. Verificato in acqua il
2026-10-20 (sessione 228 su `Easy Peasy`, lati P): `passed mark 1 (cut P)`,
`passed mark 2 (cut P)`, linea armata, `FINISHED` 162 s.

## 4. Il lato sbagliato

Stesse condizioni (aggancio + taglio d'uscita allontanandosi da B) ma dal lato
opposto a quello dichiarato: **avviso**, la boa resta il bersaglio, va rigirata.

## 5. Partenza, arrivo, porte

Restano segmenti da attraversare. Le porte si passano **attraversando il
segmento fra le due boe**.

Per partenza e arrivo: **due punti consecutivi da parti opposte della linea,
e il punto di incrocio (interpolato fra i due) dentro la linea**, cioè fra i
suoi due estremi. Non serve che un punto cada *dentro* la linea: il segmento
fra due punti lontani la attraversa comunque, quindi un buco nel feed non può
saltare l'incrocio (che è quello che è successo nella gara 181: la linea era
armata, la barca l'ha attraversata, e la regola precedente — banda di 150 m +
cambio di lato — non ha segnato niente).

**Vero accettato, con i suoi limiti** (ratificato dal velista il 2026-10-17):
il sistema vede i punti campionati, non il percorso fra due punti. A 1 Hz e
~14 m/s restano ~14 m di percorso non osservato, di più nei buchi del feed,
quindi l'incrocio è ricostruito, non visto. Si accetta così com'è: il sistema
misura quello che può misurare, la giuria osserva e contraddice. Nessun
margine agli estremi, nessuna conferma a posteriori, nessuna annotazione di
incertezza: la complessità non compra verità.

Fuori dalla legge, e solo diagnostico: il motore può **fermare** una gara che
si è dimostrata rotta (`BAD RUN`: feed illeggibile, o tre verdicti sbagliati
sulla stessa boa) invece di continuarla. Non cambia nessun verdetto di
passaggio — è un interruttore, non una regola.

## 6. Cosa NON c'è

- Nessuna distanza minima: anche a 500 metri, se i tagli sono dal lato giusto,
  vale. La vicinanza non fa parte della regola.
- Niente angoli di soglia, niente raggi in metri, niente lato armato, niente
  coni, niente freschezza, niente anti-blocco. «Allontanarsi» si legge su più
  punti di seguito (dettaglio del device, non regola).
- Il simulatore gira ogni boa dal lato dichiarato (CCW per P): è il giro che
  fa uscire la barca dal lato giusto del traverso d'uscita.

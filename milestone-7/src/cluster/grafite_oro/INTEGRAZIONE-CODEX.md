# Integrazione nel backend 6WB di nbt-aad

Aggiungi il modulo grafico al target esistente. Individua nella branch corrente il decoder degli eventi nav Android Auto, lo stato della manovra e della distanza, e il produttore dei comandi vettoriali per 6WB. I nomi del kit sono identificativi interni, quindi crea un mapping esplicito dagli enum e dai campi effettivi del backend. Usa lo stesso flusso di normalizzazione per Google Maps e Waze, tenendo conto delle differenze che emergono dai log.

| `describe(maneuver).policy` | Comportamento sul 6WB |
|---|---|
| `DistanceBased` | Solo le manovre a freccia cambiano tra vista dall’alto e prospettica al variare della distanza dalla manovra |
| `FlatOnly` | Rotatorie, ingresso e uscita di rotatoria, traghetti, traghetti ferroviari e relative varianti vanno sempre sul piano centrale non prospettico. Anche la bandiera di arrivo è classificata sul piano piatto |
| `Hidden` | Nessuna grafica centrale. Rimuovi l’eventuale grafica precedente secondo il ciclo già usato dal backend |
| Corsie | Fascia dedicata 283×40, aggiornata dai dati delle corsie |

1. Verifica sulla branch e sul quadro gli handle centrali. Il riferimento JayJay riportato per il progetto associa la trasformazione prospettica a `0x1F/0x20`. La stessa documentazione raggruppa `0x21/0x22` fra le superfici prospettiche nella tabella dimensionale. Confronta le due coppie con la stessa griglia asimmetrica e con una cattura della navigazione BMW. Assegna la vista dall’alto solo dopo aver identificato il piano centrale piatto sulla F31. Verifica separatamente la resa su `0x23/0x24` per la fascia corsie. Un ACK attesta la ricezione. Per promuovere un handle serve una prova di visibilità.
2. Decidi nel backend la soglia e il senso della transizione in base al comportamento osservato e concordato, dato che il requisito stabilisce solo la dipendenza dalla distanza. Usa la distanza della manovra corrente e rivaluta al suo aggiornamento, anche quando l’identificativo della manovra resta uguale. Tratta la distanza assente come assente, con vista dall’alto come proposta di fallback. Per evitare oscillazioni presso la soglia usa isteresi a parità di manovra. A cambio passo o fine navigazione azzera la scelta precedente. Prepara il buffer inattivo del piano nuovo nell’ordine previsto dal backend, poi rendilo visibile e nascondi il vecchio piano. Verifica aggiornamenti ripetuti e assenza di frame doppi o vuoti.
3. Passa a `renderManeuver(event, rect, margin)` il rettangolo **utile in coordinate di ingresso** del piano scelto. Il kit fa il fit uniforme sul perimetro reale, tratto compreso. Sul piano prospettico misura la zona coperta dall’orizzonte e la trasformazione reale prima di definire il rettangolo e l’eventuale compensazione nel backend. Il renderer C++ emette geometria 2D.
4. Collega `renderLanes(event.lanes)` ai dati effettivamente decodificati. Mantieni l’ordine fisico e le posizioni, comprese quelle di direzione sconosciuta. Una lista vuota indica di svuotare la fascia. Il modello visivo attuale evidenzia per corsia, quindi preserva nel decoder anche un eventuale dettaglio per direzione se il protocollo lo fornisce. Prova 2, 3, 4, 5 e 6 corsie con più raccomandazioni simultanee e verifica la leggibilità effettiva.
5. Converti ogni `Shape` nell’ordine fornito. Il fill usa parità pari/dispari, necessaria per il foro centrale della rotatoria. Se il formato quadro accetta solo poligoni pieni, decomponi in poligoni semplici preservando il foro. Espandi i tratti in contorni pieni se il backend non offre draw di polilinee. Rispetta i limiti misurati di punti, payload e quantizzazione della branch target. Mappa `Color` sulla palette realmente disponibile. Il valore `rgb()` è il riferimento dell’anteprima e non un indice HUDTerminal. Usa il profilo e la tavola prova descritti sotto per verificare oro, grafite, bianco, grigio e contrasto dopo quantizzazione.
6. Per le rotatorie, le due voci compatibilità A/B sono due chiavi grafiche e hanno la stessa sagoma. Ricava l’associazione ai codici effettivi dalla branch e dalle catture, senza ricostruirla dall’ordine del catalogo. Le geometrie delle uscite 1–4 sono esempi con angoli predefiniti. Se il messaggio contiene solo il numero di uscita, usa la geometria base e preserva il numero nel percorso di testo esistente. Se contiene un angolo utile, adatta la geometria all’angolo reale o scegli l’esempio solo quando è compatibile. Le uscite arbitrarie non sono generate da questa versione del kit.

## Palette vettoriale e prova sulla F31

Aggiungi `src/hudterminal_palette.cpp` al target e includi `hudterminal_palette.hpp` nell’adattatore. `HudTerminalPalette` contiene gli indici misurati per i cinque ruoli grafici. Per ogni fill e stroke, `hudColorIndex()` distingue fra `Skip` (trasparenza del modello), `Mapped` (indice da passare al backend) e `Unmapped` (campo senza indice valido). L’indice zero può essere valido. Un profilo incompleto deve conservare il renderer già funzionante per quella superficie fino a quando i colori richiesti dalla scena sono disponibili. Carica e valida il profilo prima di iniziare a disegnare sul buffer inattivo.

Per ricavare il profilo, esponi una prova manuale nel backend esistente, a vettura ferma. `hudPaletteSwatches(rect, colonne)` produce 20 celle ordinate da sinistra verso destra e dall’alto verso il basso. Ogni cella contiene un poligono pieno e una linea dello stesso indice. Usa l’encoder vettoriale di nbt-aad, con la forma colore `0x21B [istanza:u16][indice:u8]` e gli stessi parametri dei poligoni e delle linee già funzionanti. Rispetta il budget di comandi e gli ACK del backend. Usa cinque colonne su un piano utile ampio o dieci sulle fasce basse. Tieni tutti i campioni del piano prospettico sotto l’orizzonte nelle coordinate di ingresso. Visualizza la tavola attraverso il ciclo A/B già usato e raccogli l’esito visivo per superficie e modalità. Registra anche gli indici invisibili o semitrasparenti. Il documento segnala `0x0E` come invisibile nel test citato. Verifica eventuali differenze Comfort, Sport ed Eco prima di introdurre più profili.

Seleziona il crema visibile più caldo come candidato per l’oro, il grigio scuro per la carreggiata e un grigio più chiaro leggibile per le corsie vuote. Verifica la separazione fra bordo e freccia bianca sulla grafica effettiva. Carica questi indici nel profilo solo dopo la prova. Se il nero utile alla scacchiera è trasparente sul quadro, valuta il grigio più scuro opaco disponibile. Non trattare una foto della strumentazione come misura RGB.

Per RoadIcon `0x18/0x19`, il percorso NBT_RLE32 usa i pixel in ordine ABGR con alfa zero per la trasparenza. Qui il file RGB è il riferimento per i pixel da codificare. La resa cromatica del pannello resta da osservare. Nei test di JayJay gli altri piani citati hanno restituito un ACK bitmap senza mostrarla. Frecce centrali, rotatorie e corsie restano vettoriali in questa integrazione.

## Esempio del punto di aggancio

```cpp
artwork6wb::NavigationEvent event{
    artwork6wb::Maneuver::Left,
    0,
    {{artwork6wb::LaneLeft | artwork6wb::LaneStraight, true},
     {artwork6wb::LaneStraight, false}}
};
const auto policy = artwork6wb::describe(event.maneuver).policy;
// Il backend sceglie il piano da policy e distanza prima di passare il rettangolo.
const auto central = artwork6wb::renderManeuver(event, {0, 0, 221, 158});
const auto lanes = artwork6wb::renderLanes(event.lanes);
```

Per il contratto delle superfici, consulta il [riferimento HUDTerminal di JayJay](https://github.com/JayJay3071/nbt-android-auto/blob/main/docs/CLUSTER-HUDTERMINAL.md) e verifica i risultati sulla F31.

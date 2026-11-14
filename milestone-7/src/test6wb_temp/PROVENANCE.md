# Provenienza dei moduli temporanei

Sorgente richiesto: `MLiv79/BMW-6WB-Test`, commit
`27c0291cb9dedfe7c5b1c1ac1d029b153e05b6e5`.

Copie funzionali preservate byte per byte:

- `commands/plan.cpp`, `commands/plan.hpp`;
- `patterns/patterns.cpp`, `patterns/patterns.hpp`, `patterns/identity.cpp`;
- esempi `examples/frame.txt`, `examples/ab.txt`, `examples/composition.txt`.

`profile/surfaces.hpp` cambia solo il riferimento documentale di provenienza.
Le dimensioni, la matrice metodo/handle e le costanti non sono modificate.
`configuration()` è usata dalla validazione pura; non costruisce un nuovo
HudTerminal, client MOST o connessione DSI.

`logging/log.hpp/.cpp` sono adattati per riuso fra lavori diagnostici e osservazione
del mittente esistente: reset/riapertura del file per lavoro, indirizzo/tag
campionati dal client già registrato e accodamento diretto dei metadata TX/RX.
La coda viene inizializzata nel costruttore, sul chiamante di startup; i producer
non creano un Event locale e non scrivono su file. Non ci sono listener aggiunti
alla registrazione DSI. Il logger mantiene le correzioni simboliche degli ACK
presenti nel commit sorgente.

`engine.cpp/.hpp`, `ipc.hpp` e `client.cpp` sono la sola integrazione nuova.
Non vengono importati `main`, `Session`, DsiClient, most::Client, HudTerminal
Core o codec di Test6wb: questi ultimi sono quelli già presenti in NBT-AAD.
Il repository originale Sq00ky non è stato usato come sorgente dell'integrazione.

Si applica la licenza PolyForm Noncommercial 1.0.0 del repository ospitante,
`/LICENSE.md`. Conservare gli SPDX e i Required Notice dei file importati:

> Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)

Il notice è attribuzione di licenza, non il repository di riferimento per il backend.

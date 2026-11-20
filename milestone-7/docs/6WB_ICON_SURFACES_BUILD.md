# Superfici bitmap e compilazione manuale Windows

## Configurazione delle superfici

Il parametro `NBT_6WB_ICON_SURFACE` accetta `0x18`, `0x19`, `auto`, `off`. Quando presente con un valore valido prevale su `NBT_6WB_ICON_BITMAPS`. Senza il nuovo parametro, `NBT_6WB_ICON_BITMAPS=1` mantiene la gestione automatica della coppia. Senza entrambi i parametri, i bitmap restano disabilitati come nel working tree precedente.

Per provare esclusivamente `0x18`, inserire queste righe in `nbt-aad.env`, insieme alle altre impostazioni esistenti.

```ini
NBT_CLUSTER=6wb
NBT_HUDTERM=on
NBT_6WB_ICON_BITMAPS=1
NBT_6WB_ICON_SURFACE=0x18
```

Per provare esclusivamente `0x19`, cambiare l'ultima riga.

```ini
NBT_6WB_ICON_SURFACE=0x19
```

Per alternare automaticamente i due buffer, usare `NBT_6WB_ICON_SURFACE=auto`. Per disabilitarli, usare `NBT_6WB_ICON_SURFACE=off`. Mantenere una sola occorrenza di ciascuna chiave. Il launcher HMI attuale carica al massimo 15 assegnazioni dal file, quindi queste righe devono rientrare fra quelle effettivamente lette.

## Sequenze effettive

All'avvio il renderer considera `0x19` il buffer superiore visibile. In modalità automatica prepara quindi il primo bitmap su `0x18`. Invia `220` a `0x18`, poi `C05` a `0x18` alle coordinate `(0,0)`, poi `20E(0x19,0x18)`, `204(0x19,0)` e `204(0x18,1)`. Dopo l'accettazione dei chunk aggiorna la previsione del buffer visibile. Il successivo cambio della miniatura prepara e attiva `0x19`. Il codice precedente alla selezione esplicita indirizzava già disegno e attivazione allo stesso handle.

In modalità fissa `0x18`, invia `204(0x18,0)`, `220(0x18)`, `C05(0x18)`, `204(0x19,0)` e `204(0x18,1)`. La modalità fissa `0x19` usa la sequenza speculare. Queste modalità aggiornano sempre il medesimo handle e non inviano `20E` per i bitmap. Il nascondimento temporaneo evita di mostrare un buffer fisso mentre viene sostituito. La cancellazione e l'attivazione sono comandi asincroni, nell'ordine della stessa coda.

C04 continua a interrogare gli otto handle previsti sul solo terminale `82/00`. Per la miniatura fissa è richiesta la misura compatibile del solo handle selezionato. Per la coppia automatica servono entrambe le misure. Una misura positiva conferma le dimensioni e lascia aperta la verifica visiva della superficie. Non sono state aggiunte allocazioni o inizializzazioni MOST speculative.

Il centro `0x1F/0x20` usa il proprio disegno vettoriale e il proprio stato A/B. Il bitmap superiore usa `C05` con NBT_RLE32, TX0 e dimensioni 305×44. Le due coppie conservano stati e percorsi distinti. La prova fissa serve a verificare sulla vettura l'ipotesi di alternanza per `0x18/0x19`.

## Contenuto necessario durante la prova

La miniatura ordinaria appare quando Android Auto fornisce un secondo passo rappresentabile. Nei log Maps/Waze locali già analizzati compare soltanto il primo passo. Se manca il secondo passo e manca uno scudetto abilitato esplicitamente, il codice nasconde le superfici superiori anche quando il loro handle è selezionato.

Il log ordinario del daemon riporta `6wb icons` con modalità, handle di disegno, altro handle, disponibilità delle dimensioni e presenza di contenuto. Il valore di visibilità riportato indica il comando previsto. Un messaggio separato segnala un bitmap che supera il limite C05. Queste informazioni permettono di verificare prima se è stato richiesto un disegno. Il log C04 rimane `/opt/aa/logs/6wb-surfaces.log` nell'installazione manuale.

## Compilazione manuale con QNX SDP 6.5 e SP1

Lo script `build-6wb.cmd` avvia `build-6wb.ps1` con Windows PowerShell 5.1. Il percorso SDK predefinito è `C:\Program Files (x86)\QNX6.5`. Lo script risolve i percorsi brevi Windows per `QNX_HOST` e `QNX_TARGET`, evitando il problema degli spazi nel driver GCC 4.4. Richiede i percorsi brevi già disponibili oppure un percorso SDK esistente senza spazi.

Da PowerShell, eseguire manualmente questi comandi.

```powershell
$env:QNX_HOST = 'C:/Program Files (x86)/QNX6.5/host/win32/x86'
$env:QNX_TARGET = 'C:/Program Files (x86)/QNX6.5/target/qnx6'
$env:PATH = "$env:QNX_HOST/usr/bin;$env:PATH"
Set-Location -LiteralPath 'C:\Users\memel\BMW-6WB\milestone-7'
.\build-6wb.cmd
```

Da `cmd.exe`, eseguire manualmente questi comandi.

```bat
set "QNX_HOST=C:/Program Files (x86)/QNX6.5/host/win32/x86"
set "QNX_TARGET=C:/Program Files (x86)/QNX6.5/target/qnx6"
set "PATH=%QNX_HOST%/usr/bin;%PATH%"
cd /d C:\Users\memel\BMW-6WB\milestone-7
build-6wb.cmd
```

Per cambiare soltanto il numero di processi, usare `build-6wb.cmd -Jobs 2`. Lo script invoca il Makefile esistente con `-B`, il target `gcc_ntoarmv7le_cpp` e una destinazione specifica passata mediante `TARGET`. Il target C resta `gcc_ntoarmv7le`. Le opzioni ARMv7, VFPv3, softfp e GNU C++0x rimangono quelle del Makefile.

Ogni esecuzione crea una nuova directory sotto `milestone-7\output\6wb-manual\`, identificata da data e suffisso univoco. Se esiste `milestone-7\nbt-aad`, lo copia in quella directory come `previous-nbt-aad`. Il nuovo eseguibile viene collegato direttamente nella nuova directory con nome `nbt-aad`. Gli oggetti intermedi vengono ricostruiti nelle directory sorgenti previste dal Makefile.

Lo script verifica magic ELF, classe ELF32, little-endian, tipo ET_EXEC e macchina ARM, poi usa `ntoarmv7-readelf.exe -h -l -d` e controlla l'interprete `/usr/lib/ldqnx.so.2`. Registra percorso, dimensione e SHA-256 in `build-info.txt`, intestazioni e dipendenze in `elf.txt`, e output della compilazione in `build.log`. Le librerie locali richieste sono `liba2i_client.so`, `libscreen.so`, `libssl.so`, `libcrypto.so`. Le dipendenze QNX di sistema rimangono socket, usbdi, libcpp, libm e libc, con RPATH `/opt/mm/lib:/lib:/usr/lib`.

Per leggere il risultato più recente da PowerShell, dopo una compilazione riuscita, eseguire manualmente questi comandi.

```powershell
$build = Get-ChildItem -LiteralPath '.\output\6wb-manual' -Directory |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
Get-Content -LiteralPath (Join-Path $build.FullName 'build-info.txt')
Get-Content -LiteralPath (Join-Path $build.FullName 'elf.txt')
Get-Item -LiteralPath (Join-Path $build.FullName 'nbt-aad')
```

Il percorso finale avrà la forma `C:\Users\memel\BMW-6WB\milestone-7\output\6wb-manual\<identificativo>\nbt-aad`. La procedura opera sul PC locale. I nuovi file di script e documentazione non entrano nelle liste dei sorgenti C++. La configurazione CMake secondaria è stata allineata al driver qcc, alle opzioni ARM del Makefile e alle librerie mancanti. La procedura manuale descritta usa Make.

Le verifiche di questa sessione sono statiche. La compatibilità effettiva del collegamento e la visibilità delle superfici si verificano rispettivamente con la compilazione manuale e con la prova sul quadro.

#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Builds NbtAad_ALL_Texts.zip (<lang>.txt with "id=text" lines, same format as BMW's PluginsCoded apps)
# and NbtAad_ALL_Images.zip (<imageId>.png).
import os, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Espanol", "es-US": "Espanol (US)",
         "fr-FR": "Francais", "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian", "pl": "Polski", "hu": "Hungarian"}
ABOUT = ("NBT-AAD: native wireless Android Auto\\nDeveloped by nanaisu\\n"
         "Contributors: @houssems, @tom-pryor, @dn00, @sabifa,\\n"
         "@ryanriccio1, @pstryju, @owenashurst, @BobW5, @DreckSoft\\n"
         "github.com/Sq00ky/Supra-iDrive6-Android-Auto\\n"
         "Free of charge. If you paid for it, you were scammed.")
TEXTS = {1: "Android Auto", 2: "Android Auto: Connecting...",   # 2 = entry text while nbt-aad is connecting
         # Android Auto menu (hmiState 10): row 1 changes with the daemon state (10 / 13 / 14)
         10: "Start Android Auto", 11: "Force Stop", 12: "Restart", 13: "Connecting...", 14: "Open Android Auto",
         # 15 = "start at boot" checkbox, 16 = About row / page title, 17 = About page text (hmiState 12): a Richtext
         # list wraps it to the screen width; "\n" (backslash + n in the .txt) is a line break, as in BMW's own
         # ActiveSeating text 88. English only.
         15: "Start automatically", 16: "About", 18: "Save logs to USB",   # 18 = log checkbox (rhmiapp-10)
         19: "MENU key opens iDrive",   # 19 = MENU key checkbox (rhmiapp-14)
         20: "Show driving mode changes",   # 20 = driving mode checkbox (rhmiapp-21)
         21: "Show navigation in HUD/gauge cluster",   # 21 = cluster/HUD arrows checkbox (kombi_nav); renamed in
         45: "Version",                               # rhmiapp-55 to say what it does across cluster + HUD
         46: "Show directions at any distance",       # 45 = Version row/page title, 46 = the "at any distance" box
         47: "Disable logging (performance test)",     # 47 = no-log checkbox (rhmiapp-56); env NBT_NO_LOG wins
         52: "Nav prompts on their own channel",   # 52 = native audio: prompts on the nav prompt channel (default on)
         51: "Switch 2 Finger Gesture to Mute",   # 51 = gesture checkbox (native audio; 50 is kept for PR #285)
         49: "Song title in gauge cluster",   # 49 = now-playing checkbox (kombi_nav idle text, 6WA street line)
         48: "Prompts on car radio (BT, beta)",     # 48 = BT speech checkbox (rhmiapp-66)
         # 400 / 404-406 = the gauge cluster type row, page title and its three choices.
         400: "Gauge cluster type",
         404: "Supra (J29)",
         405: "F-series (6WA)",
         406: "6WB (no driver yet)",
         407: "Cyrillic street names in HUD",   # 407 = 6WA HUD Cyrillic checkbox (rhmiapp-99)
         22: "Show iDrive warnings",   # 22 = iDrive warnings checkbox (rhmiapp-32)
         23: "Settings",   # 23 = Settings row (hmiState 10) / page title (hmiState 14), rhmiapp-39
         24: "Android Auto over USB",   # 24 = USB checkbox on the Settings page (rhmiapp-40)
         25: "Use Phone GPS",   # 25 = phone GPS checkbox on the Settings page (rhmiapp-44)
         26: "Use WiFi Direct Network (own password)",   # 26 = Wi-Fi Direct only checkbox on the Settings page (rhmiapp-44)
         27: "Use Native Audio instead of Bluetooth",   # 27 = browser audio checkbox (rhmiapp-45)
         28: "Native Functionality Media Controls Restore (beta)",   # 28 = steering-wheel media keys checkbox (rhmiapp-45)
         # 29-31 = the PF rows on the Settings page, 32-43 = their step / result texts (rhmiapp-47)
         29: "Install PF Rules", 30: "Check PF Rules", 31: "Uninstall PF Rules",
         32: "Writing rules...", 33: "Rules successfully written", 34: "Rules active",
         35: "Checking rules...", 36: "Rules in place", 37: "Rules in place, not active", 38: "Rules not in place",
         39: "Removing rules...", 40: "Rules successfully removed", 41: "Packet filter off, as before",
         42: "Done, not active - see log", 43: "Failed - see log",
         # 320-325 = the entry row and the Start row carry the daemon state (rhmiapp-98).
         320: "Android Auto",
         321: "Connecting to the phone...",
         322: "Connected",
         323: "Waiting for the phone",
         324: "Android Auto: no Wi-Fi Direct, use USB",
         325: "Android Auto: no CarPlay network, using Wi-Fi Direct",
         326: "Waiting for phone on CarPlay Wi-Fi",
         327: "No CarPlay: enable WiFi Direct",
         328: "No CarPlay or WiFi Direct: use USB",
         329: "Connected over Wi-Fi",
         330: "Connected over USB",
         331: "Connecting to the phone...",
         332: "Waiting for phone over USB",
         333: "Waiting: CarPlay Wi-Fi",
         334: "No CarPlay: enable WiFi Direct",
         # 335-338 (rhmiapp-100): the network really offered, and why a waiting phone is stuck.
         335: "Waiting for phone on Wi-Fi Direct",
         336: "No Bluetooth link: forget phone in car, pair again",
         337: "Phone not on Wi-Fi: forget phone in car, pair again",
         338: "Car refused phone: see coding-requirements.md (GitHub)",
         339: "Waiting for Bluetooth device",   # 339 = no phone on Bluetooth (rhmiapp-90)
         340: "No usable Wi-Fi in car: use USB",                   # rhmiapp-91
         341: "Can't use WiFi Direct: set SSID name manually",     # rhmiapp-91 (NBT_WFD_SSID)


         44: "CarPlay compatibility",   # 44 = CarPlay compatibility checkbox (rhmiapp-54-cpcompat)
         17: ABOUT}
# Per-language overrides (ASCII only, like the language names above).
LANG_TEXTS = {
    "de": {52: "Navigationsansagen auf eigenem Kanal", 51: "Zwei-Finger-Geste auf Stumm schalten", 49: "Songtitel im Kombi", 340: "Kein nutzbares WLAN im Auto: USB verwenden", 341: "WLAN-Direct nicht nutzbar: SSID-Namen manuell setzen", 339: "Warte auf Bluetooth-Gerät", 335: "Warte auf Telefon über WLAN-Direct", 336: "Kein Bluetooth: Telefon im Auto löschen, neu koppeln", 337: "Telefon nicht im WLAN: im Auto löschen, neu koppeln", 338: "Auto lehnt Telefon ab: coding-requirements.md (GitHub)", 48: "Ansagen über das Radio (BT, Beta)", 22: "iDrive-Warnungen anzeigen", 21: "Navigation in Kombi/HUD anzeigen", 47: "Protokollierung aus (Leistungstest)", 45: "Version", 46: "Richtungen ohne Entfernungslimit", 20: "Fahrmodus-Wechsel anzeigen", 19: "MENU-Taste öffnet iDrive", 2: "Android Auto: Verbinden...", 18: "Protokolle auf USB speichern", 10: "Android Auto starten", 11: "Beenden erzwingen", 12: "Neu starten",
           13: "Verbinden...", 14: "Android Auto öffnen", 15: "Automatisch starten", 16: "Info", 23: "Einstellungen", 24: "Android Auto über USB", 25: "Telefon-GPS verwenden", 26: "WLAN-Direct-Netz verwenden (eigenes Passwort)", 27: "Natives Audio statt Bluetooth", 28: "Native Mediensteuerung wiederherstellen (Beta)", 29: "PF-Regeln installieren", 30: "PF-Regeln prüfen", 31: "PF-Regeln entfernen", 32: "Regeln werden geschrieben...", 33: "Regeln erfolgreich geschrieben", 34: "Regeln aktiv", 35: "Regeln werden geprüft...", 36: "Regeln vorhanden", 37: "Regeln vorhanden, nicht aktiv", 38: "Keine Regeln vorhanden", 39: "Regeln werden entfernt...", 40: "Regeln erfolgreich entfernt", 41: "Paketfilter aus, wie zuvor", 42: "Fertig, nicht aktiv - siehe Log", 43: "Fehlgeschlagen - siehe Log", 44: "CarPlay-Kompatibilität",
           17: "NBT-AAD: Native Android Auto Implementierung für NBT Evo.\\nEntwickelt von nanaisu.\\nMit Beiträgen von @houssems, @tom-pryor, @dn00, @sabifa,\\n@ryanriccio1, @pstryju, @owenashurst, @BobW5, @DreckSoft.\\ngithub.com/Sq00ky/Supra-iDrive6-Android-Auto\\nKostenlos. Falls du dafür bezahlt hast, wurdest du abgezockt."},
    "es-ES": {52: "Indicaciones en canal propio", 51: "Gesto de dos dedos: silenciar", 49: "Titulo de la cancion en el cuadro", 340: "Sin Wi-Fi utilizable en el coche: use USB", 341: "Wi-Fi Direct no disponible: fije el SSID a mano", 339: "Esperando dispositivo Bluetooth", 335: "Esperando el telefono por Wi-Fi Direct", 336: "Sin Bluetooth: borre el telefono del coche y emparejelo", 337: "Telefono sin Wi-Fi: borrelo del coche y emparejelo", 338: "El coche rechaza el telefono: coding-requirements.md (GitHub)", 48: "Avisos por la radio (BT, beta)", 22: "Mostrar avisos de iDrive", 21: "Mostrar navegacion en cuadro/HUD", 47: "Desactivar registro (prueba de rendimiento)", 45: "Version", 46: "Indicaciones a cualquier distancia", 20: "Mostrar cambios de modo de conduccion", 19: "La tecla MENU abre iDrive", 2: "Android Auto: Conectando...", 18: "Guardar registros en USB", 10: "Iniciar Android Auto", 11: "Forzar cierre", 12: "Reiniciar",
              13: "Conectando...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Acerca de", 23: "Ajustes", 24: "Android Auto por USB", 25: "Usar el GPS del telefono", 26: "Usar la red Wi-Fi Direct (contrasena propia)", 27: "Usar audio nativo en lugar de Bluetooth", 28: "Restaurar controles multimedia nativos (beta)", 29: "Instalar reglas PF", 30: "Comprobar reglas PF", 31: "Desinstalar reglas PF", 32: "Escribiendo reglas...", 33: "Reglas escritas correctamente", 34: "Reglas activas", 35: "Comprobando reglas...", 36: "Reglas instaladas", 37: "Reglas instaladas, no activas", 38: "Reglas no instaladas", 39: "Eliminando reglas...", 40: "Reglas eliminadas correctamente", 41: "Filtro desactivado, como antes", 42: "Hecho, no activo - ver registro", 43: "Error - ver registro", 44: "Compatibilidad con CarPlay"},
    "fr-FR": {52: "Instructions sur un canal dédié", 51: "Geste à deux doigts : couper le son", 49: "Titre du morceau au combine", 340: "Aucun Wi-Fi utilisable : utilisez l'USB", 341: "Wi-Fi Direct inutilisable : definissez le SSID", 339: "En attente d'un appareil Bluetooth", 335: "En attente du telephone sur Wi-Fi Direct", 336: "Pas de Bluetooth : supprimez le telephone, rejumelez", 337: "Telephone hors Wi-Fi : supprimez-le de la voiture, rejumelez", 338: "La voiture refuse le telephone : coding-requirements.md (GitHub)", 48: "Annonces via la radio (BT, beta)", 22: "Afficher les avertissements iDrive", 21: "Afficher la navigation combine/HUD", 47: "Desactiver les journaux (test de performance)", 45: "Version", 46: "Directions a toute distance", 20: "Afficher les changements de mode de conduite", 19: "La touche MENU ouvre iDrive", 2: "Android Auto : Connexion...", 18: "Enregistrer les journaux sur USB", 10: "Demarrer Android Auto", 11: "Forcer l'arret", 12: "Redemarrer",
              13: "Connexion...", 14: "Ouvrir Android Auto", 15: "Demarrage automatique", 16: "A propos", 23: "Parametres", 24: "Android Auto par USB", 25: "Utiliser le GPS du telephone", 26: "Utiliser le reseau Wi-Fi Direct (mot de passe propre)", 27: "Utiliser l'audio natif au lieu du Bluetooth", 28: "Restaurer les commandes multimedia natives (beta)", 29: "Installer les regles PF", 30: "Verifier les regles PF", 31: "Desinstaller les regles PF", 32: "Ecriture des regles...", 33: "Regles ecrites avec succes", 34: "Regles actives", 35: "Verification des regles...", 36: "Regles en place", 37: "Regles en place, non actives", 38: "Regles absentes", 39: "Suppression des regles...", 40: "Regles supprimees avec succes", 41: "Filtre desactive, comme avant", 42: "Fait, non actif - voir journal", 43: "Echec - voir journal", 44: "Compatibilite CarPlay"},
    "it": {52: "Indicazioni su canale dedicato", 51: "Gesto a due dita: silenzia", 49: "Titolo del brano nel quadro", 340: "Nessun Wi-Fi utilizzabile in auto: usa USB", 341: "Wi-Fi Direct non usabile: imposta SSID a mano", 339: "In attesa del dispositivo Bluetooth", 335: "In attesa del telefono su Wi-Fi Direct", 336: "Nessun Bluetooth: elimina il telefono dall'auto e riabbina", 337: "Telefono non in Wi-Fi: eliminalo dall'auto e riabbina", 338: "L'auto rifiuta il telefono: coding-requirements.md (GitHub)", 48: "Annunci via radio (BT, beta)", 22: "Mostra gli avvisi di iDrive", 21: "Mostra navigazione in quadro/HUD", 47: "Disattiva log (test prestazioni)", 45: "Versione", 46: "Indicazioni a qualsiasi distanza", 20: "Mostra i cambi di modalita di guida", 19: "Il tasto MENU apre iDrive", 2: "Android Auto: Connessione...", 18: "Salva i log su USB", 10: "Avvia Android Auto", 11: "Arresto forzato", 12: "Riavvia",
           13: "Connessione...", 14: "Apri Android Auto", 15: "Avvio automatico", 16: "Informazioni", 23: "Impostazioni", 24: "Android Auto via USB", 25: "Usa il GPS del telefono", 26: "Usa la rete Wi-Fi Direct (password propria)", 27: "Usa l'audio nativo invece del Bluetooth", 28: "Ripristina comandi multimediali nativi (beta)", 29: "Installa regole PF", 30: "Verifica regole PF", 31: "Disinstalla regole PF", 32: "Scrittura regole...", 33: "Regole scritte correttamente", 34: "Regole attive", 35: "Verifica regole...", 36: "Regole presenti", 37: "Regole presenti, non attive", 38: "Regole assenti", 39: "Rimozione regole...", 40: "Regole rimosse correttamente", 41: "Filtro disattivato, come prima", 42: "Fatto, non attivo - vedi log", 43: "Errore - vedi log", 44: "Compatibilita CarPlay"},
    "pt": {52: "Instruções de navegação em canal próprio", 51: "Gesto de dois dedos: silenciar", 49: "Titulo da musica no painel", 340: "Sem Wi-Fi utilizavel no carro: use USB", 341: "Wi-Fi Direct indisponivel: defina o SSID", 339: "A aguardar dispositivo Bluetooth", 335: "A aguardar o telefone por Wi-Fi Direct", 336: "Sem Bluetooth: apague o telefone no carro e emparelhe", 337: "Telefone sem Wi-Fi: apague-o no carro e emparelhe", 338: "O carro recusa o telefone: coding-requirements.md (GitHub)", 48: "Avisos pela radio (BT, beta)", 22: "Mostrar avisos do iDrive", 21: "Mostrar navegacao no painel/HUD", 47: "Desativar registo (teste de desempenho)", 45: "Versao", 46: "Indicacoes a qualquer distancia", 20: "Mostrar mudancas de modo de conducao", 19: "A tecla MENU abre o iDrive", 2: "Android Auto: A ligar...", 18: "Guardar registos em USB", 10: "Iniciar Android Auto", 11: "Forcar paragem", 12: "Reiniciar",
           13: "A ligar...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Sobre", 23: "Definicoes", 24: "Android Auto por USB", 25: "Usar o GPS do telefone", 26: "Usar a rede Wi-Fi Direct (palavra-passe propria)", 27: "Usar audio nativo em vez de Bluetooth", 28: "Restaurar controlos multimedia nativos (beta)", 29: "Instalar regras PF", 30: "Verificar regras PF", 31: "Desinstalar regras PF", 32: "A escrever regras...", 33: "Regras escritas com sucesso", 34: "Regras ativas", 35: "A verificar regras...", 36: "Regras instaladas", 37: "Regras instaladas, nao ativas", 38: "Regras nao instaladas", 39: "A remover regras...", 40: "Regras removidas com sucesso", 41: "Filtro desligado, como antes", 42: "Feito, nao ativo - ver registo", 43: "Falhou - ver registo", 44: "Compatibilidade CarPlay"},
    "pl": {52: "Komunikaty nawigacji na osobnym kanale", 51: "Gest dwoma palcami: wycisz", 49: "Tytuł utworu na zegarach", 340: "Brak użytecznego Wi-Fi w aucie: użyj USB", 341: "Wi-Fi Direct nieużywalne: ustaw SSID ręcznie", 339: "Czekam na urządzenie Bluetooth", 335: "Czekam na telefon przez Wi-Fi Direct", 336: "Brak Bluetooth: usuń telefon w aucie i sparuj ponownie", 337: "Telefon nie w Wi-Fi: usuń go w aucie, sparuj ponownie", 338: "Auto odrzuca telefon: coding-requirements.md (GitHub)", 48: "Komunikaty głosowe przez radio (BT, beta)", 22: "Pokazuj ostrzeżenia iDrive", 21: "Pokazuj nawigację na zegarach/HUD", 47: "Wyłącz zapis logów (test wydajności)", 45: "Wersja", 46: "Pokazuj wskazówki niezależnie od odległości", 20: "Pokazuj zmiany trybu jazdy", 19: "Przycisk MENU otwiera iDrive", 2: "Android Auto: Łączenie...", 18: "Zapisuj logi na USB", 10: "Uruchom Android Auto", 11: "Wymuś zatrzymanie", 12: "Uruchom ponownie", 1: "Android Auto",
           13: "Łączenie...", 14: "Otwórz Android Auto", 15: "Uruchamiaj automatycznie", 16: "Informacje", 23: "Ustawienia", 24: "Android Auto przez USB", 25: "Używaj GPS telefonu", 26: "Używaj sieci Wi-Fi Direct (własne hasło)", 27: "Używaj natywnego audio zamiast Bluetooth", 28: "Przywróć natywne sterowanie multimediami (beta)", 29: "Zainstaluj reguły PF", 30: "Sprawdź reguły PF", 31: "Odinstaluj reguły PF", 32: "Zapisywanie reguł...", 33: "Reguły zapisane pomyślnie", 34: "Reguły aktywne", 35: "Sprawdzanie reguł...", 36: "Reguły zainstalowane", 37: "Reguły zainstalowane, ale nieaktywne", 38: "Brak zainstalowanych reguł", 39: "Usuwanie reguł...", 40: "Reguły usunięte pomyślnie", 41: "Filtr pakietów wyłączony, jak wcześniej", 42: "Gotowe, reguły nieaktywne - sprawdź log", 43: "Błąd - sprawdź log", 44: "Zgodność z CarPlay",
           17: "NBT-AAD: natywne, bezprzewodowe Android Auto dla NBT Evo.\\nAutor: nanaisu.\\nWspółtwórcy: @houssems, @tom-pryor, @dn00, @sabifa,\\n@ryanriccio1, @pstryju, @owenashurst, @BobW5, @DreckSoft.\\ngithub.com/Sq00ky/Supra-iDrive6-Android-Auto\\nBezpłatne. Jeśli za to zapłaciłeś, zostałeś oszukany."},
    "hu": {
        2: "Android Auto: Csatlakozás...",                          # Android Auto: Connecting...
        10: "Android Auto indítása",                                # Start Android Auto
        11: "Kényszerített leállítás",                              # Force Stop
        12: "Újraindítás",                                          # Restart
        13: "Csatlakozás...",                                       # Connecting...
        14: "Android Auto megnyitása",                              # Open Android Auto
        15: "Automatikus indítás",                                  # Start automatically
        16: "Névjegy",                                              # About
        17: "NBT-AAD: Natív Android Auto implementáció az NBT Evóhoz.\\nFejlesztette: nanaisu.\\nKözreműködők: @houssems, @tom-pryor, @dn00, @sabifa,\\n@ryanriccio1, @pstryju, @owenashurst, @BobW5, @DreckSoft.\\ngithub.com/Sq00ky/Supra-iDrive6-Android-Auto\\nIngyenes. Ha fizettél érte, átvertek.",
        18: "Naplók mentése USB-re",                                # Save logs to USB
        19: "A MENU gomb az iDrive-ra visz",                        # MENU key opens iDrive
        20: "Menetmód-váltás megjelenítése",                        # Show driving mode changes
        21: "Navigáció megjelenítése a műszerfalon/HUD-on",         # Show navigation in HUD/gauge cluster
        22: "iDrive-figyelmeztetések megjelenítése",                # Show iDrive warnings
        23: "Beállítások",                                          # Settings
        24: "Android Auto USB-kapcsolaton",                  # Android Auto over USB (beta)
        25: "Telefon GPS használata",                               # Use Phone GPS
        26: "WiFi Direct hálózat használata (saját jelszóval)",     # Use WiFi Direct Network (own password)
        27: "Natív hang Bluetooth helyett",                         # Use Native Audio instead of Bluetooth
        28: "Natív médiavezérlés visszaállítása (béta)",                         # Media Controls for LCI Steering Wheels
        29: "Tűzfal-szabályok telepítése",                          # Install PF Rules
        30: "Tűzfal-szabályok ellenőrzése",                         # Check PF Rules
        31: "Tűzfal-szabályok eltávolítása",                        # Uninstall PF Rules
        32: "Szabályok írása...",                                   # Writing rules...
        33: "Szabályok sikeresen kiírva",                           # Rules successfully written
        34: "Szabályok aktívak",                                    # Rules active
        35: "Szabályok ellenőrzése...",                             # Checking rules...
        36: "Szabályok jelen vannak",                               # Rules in place
        37: "Szabályok jelen vannak, de nem aktívak",               # Rules in place, not active
        38: "Nincsenek szabályok",                                  # Rules not in place
        39: "Szabályok eltávolítása...",                            # Removing rules...
        40: "Szabályok sikeresen eltávolítva",                      # Rules successfully removed
        41: "Csomagszűrő kikapcsolva, mint korábban",               # Packet filter off, as before
        42: "Kész, nem aktív - lásd a logot",                       # Done, not active - see log
        43: "Sikertelen - lásd a logot",                            # Failed - see log
        44: "CarPlay-kompatibilitás",                               # CarPlay compatibility
        45: "Verzió",                                               # Version
        46: "Navigációs utasítások távolságtól függetlenül",        # Show directions at any distance
        47: "Naplózás kikapcsolása (teljesítményteszt)",            # Disable logging (performance test)
        48: "Navigációs hangok a rádión (BT, béta)",                # Prompts on car radio (BT, beta)
        # 320-338: állapotsorok (belépő sor és Start sor), rhmiapp-98 / rhmiapp-100
        320: "Android Auto",                                        # Android Auto
        321: "Csatlakozás a telefonhoz...",                         # Connecting to the phone...
        322: "Csatlakoztatva",                                      # Connected
        323: "Várakozás a telefonra",                               # Waiting for the phone
        324: "Android Auto: nincs Wi-Fi Direct, használj USB csatlakozást",    # Android Auto: no Wi-Fi Direct, use USB
        325: "Android Auto: nincs CarPlay-hálózat, Wi-Fi Direct használata",  # Android Auto: no CarPlay network, using Wi-Fi Direct
        326: "Várakozás a telefonra CarPlay Wi-Fi-n",               # Waiting for phone on CarPlay Wi-Fi
        327: "Nincs CarPlay: kapcsold be a WiFi Directet",          # No CarPlay: enable WiFi Direct
        328: "Nincs CarPlay és WiFi Direct: használj USB csatlakozást",        # No CarPlay or WiFi Direct: use USB
        329: "Csatlakoztatva Wi-Fi-n",                              # Connected over Wi-Fi
        330: "Csatlakoztatva USB-n",                                # Connected over USB
        331: "Csatlakozás a telefonhoz...",                         # Connecting to the phone...
        332: "Várakozás a telefonra USB-n",                         # Waiting for phone over USB
        333: "Várakozás: CarPlay Wi-Fi",                            # Waiting: CarPlay Wi-Fi
        334: "Nincs CarPlay: kapcsold be a WiFi Directet",          # No CarPlay: enable WiFi Direct
        335: "Várakozás a telefonra Wi-Fi Directen",                # Waiting for phone on Wi-Fi Direct
        336: "Nincs Bluetooth: töröld a telefont az autóban, párosítsd újra",  # No Bluetooth link: forget phone in car, pair again
        337: "Telefon nincs Wi-Fi-n: töröld az autóban, párosítsd újra",       # Phone not on Wi-Fi: forget phone in car, pair again
        338: "Az autó elutasítja a telefont: lásd coding-requirements.md (GitHub)",  # Car refused phone: see coding-requirements.md (GitHub)
        # 400-407: műszerfal típusa (sor, oldalcím, három választás) és a 6WA HUD cirill opció
        400: "Műszerfal típusa",                                    # Gauge cluster type
        404: "Supra (J29)",                                         # Supra (J29)
        405: "F-széria (6WA)",                                      # F-series (6WA)
        406: "6WB (még nem támogatott)",                            # 6WB (no driver yet)
        407: "Cirill betűs utcanevek a HUD-on",                     # Cyrillic street names in HUD
    },
}
LANG_TEXTS["es-US"] = LANG_TEXTS["es-ES"]
LANG_TEXTS["fr-CA"] = LANG_TEXTS["fr-FR"]
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Texts.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for lang, langname in LANGS.items():
        texts = dict(TEXTS)
        texts.update(LANG_TEXTS.get(lang, {}))
        body = "100000=%s\n" % langname + "".join("%d=%s\n" % (k, v) for k, v in sorted(texts.items()))
        z.writestr(lang + ".txt", body.encode("utf-8"))
icon = os.path.join(HERE, "..", "plugin", "res", "aa_row_icon_63x54.png")
preview = os.path.join(HERE, "..", "plugin", "res", "aa_content_preview_549x484.png")
with zipfile.ZipFile(os.path.join(HERE, "NbtAad_ALL_Images.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.write(icon, "55001.png")                      # 63x54 row icon (imageIdModel 4)
    z.write(preview, "55002.png")                   # 549x484 content preview (imageIdModel 12, StatePreview 13)
    for f in range(8):                              # 55010..55017: entry-row "connecting" spinner frames (rhmiapp-6)
        z.write(os.path.join(HERE, "..", "plugin", "res", "spinner", "spin_%d.png" % f), "%d.png" % (55010 + f))
print("wrote NbtAad_ALL_Texts.zip, NbtAad_ALL_Images.zip")

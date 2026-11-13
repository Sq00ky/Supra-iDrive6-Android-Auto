#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Builds NbtAad_ALL_Texts.zip (<lang>.txt with "id=text" lines, same format as BMW's PluginsCoded apps)
# and NbtAad_ALL_Images.zip (<imageId>.png).
import os, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Espanol", "es-US": "Espanol (US)",
         "fr-FR": "Francais", "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian"}
ABOUT = ("NBT-AAD: native wireless Android Auto\\nDeveloped by nanaisu\\n"
         "With contributions from @houssems, Tom Pryor (@tom-pryor) and @dn00\\n"
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
         48: "1080p over USB",                         # 48 = 1080p-over-USB checkbox (rhmiapp-59)
         22: "Show iDrive warnings",   # 22 = iDrive warnings checkbox (rhmiapp-32)
         23: "Settings",   # 23 = Settings row (hmiState 10) / page title (hmiState 14), rhmiapp-39
         24: "Android Auto over USB (beta)",   # 24 = USB checkbox on the Settings page (rhmiapp-40)
         25: "Use Phone GPS",   # 25 = phone GPS checkbox on the Settings page (rhmiapp-44)
         26: "Use WiFi Direct Network",   # 26 = Wi-Fi Direct only checkbox on the Settings page (rhmiapp-44)
         27: "Use Native Audio instead of Bluetooth",   # 27 = browser audio checkbox (rhmiapp-45)
         28: "Media Controls for LCI Steering Wheels",   # 28 = steering-wheel media keys checkbox (rhmiapp-45)
         # 29-31 = the PF rows on the Settings page, 32-43 = their step / result texts (rhmiapp-47)
         29: "Install PF Rules", 30: "Check PF Rules", 31: "Uninstall PF Rules",
         32: "Writing rules...", 33: "Rules successfully written", 34: "Rules active",
         35: "Checking rules...", 36: "Rules in place", 37: "Rules in place, not active", 38: "Rules not in place",
         39: "Removing rules...", 40: "Rules successfully removed", 41: "Packet filter off, as before",
         42: "Done, not active - see log", 43: "Failed - see log",
         44: "CarPlay compatibility",   # 44 = CarPlay compatibility checkbox (rhmiapp-54-cpcompat)
         17: ABOUT}
# Per-language overrides (ASCII only, like the language names above).
LANG_TEXTS = {
    "de": {48: "1080p uber USB", 22: "iDrive-Warnungen anzeigen", 21: "Navigation in Kombi/HUD anzeigen", 47: "Protokollierung aus (Leistungstest)", 45: "Version", 46: "Richtungen ohne Entfernungslimit", 20: "Fahrmodus-Wechsel anzeigen", 19: "MENU-Taste öffnet iDrive", 2: "Android Auto: Verbinden...", 18: "Protokolle auf USB speichern", 10: "Android Auto starten", 11: "Beenden erzwingen", 12: "Neu starten",
           13: "Verbinden...", 14: "Android Auto öffnen", 15: "Automatisch starten", 16: "Info", 23: "Einstellungen", 24: "Android Auto über USB (Beta)", 25: "Telefon-GPS verwenden", 26: "WLAN-Direct-Netz verwenden", 27: "Natives Audio statt Bluetooth", 28: "Medientasten am LCI-Lenkrad", 29: "PF-Regeln installieren", 30: "PF-Regeln prüfen", 31: "PF-Regeln entfernen", 32: "Regeln werden geschrieben...", 33: "Regeln erfolgreich geschrieben", 34: "Regeln aktiv", 35: "Regeln werden geprüft...", 36: "Regeln vorhanden", 37: "Regeln vorhanden, nicht aktiv", 38: "Keine Regeln vorhanden", 39: "Regeln werden entfernt...", 40: "Regeln erfolgreich entfernt", 41: "Paketfilter aus, wie zuvor", 42: "Fertig, nicht aktiv - siehe Log", 43: "Fehlgeschlagen - siehe Log", 44: "CarPlay-Kompatibilität"},
    "es-ES": {48: "1080p por USB", 22: "Mostrar avisos de iDrive", 21: "Mostrar navegacion en cuadro/HUD", 47: "Desactivar registro (prueba de rendimiento)", 45: "Version", 46: "Indicaciones a cualquier distancia", 20: "Mostrar cambios de modo de conduccion", 19: "La tecla MENU abre iDrive", 2: "Android Auto: Conectando...", 18: "Guardar registros en USB", 10: "Iniciar Android Auto", 11: "Forzar cierre", 12: "Reiniciar",
              13: "Conectando...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Acerca de", 23: "Ajustes", 24: "Android Auto por USB (beta)", 25: "Usar el GPS del telefono", 26: "Usar la red Wi-Fi Direct", 27: "Usar audio nativo en lugar de Bluetooth", 28: "Controles multimedia del volante LCI", 29: "Instalar reglas PF", 30: "Comprobar reglas PF", 31: "Desinstalar reglas PF", 32: "Escribiendo reglas...", 33: "Reglas escritas correctamente", 34: "Reglas activas", 35: "Comprobando reglas...", 36: "Reglas instaladas", 37: "Reglas instaladas, no activas", 38: "Reglas no instaladas", 39: "Eliminando reglas...", 40: "Reglas eliminadas correctamente", 41: "Filtro desactivado, como antes", 42: "Hecho, no activo - ver registro", 43: "Error - ver registro", 44: "Compatibilidad con CarPlay"},
    "fr-FR": {48: "1080p via USB", 22: "Afficher les avertissements iDrive", 21: "Afficher la navigation combine/HUD", 47: "Desactiver les journaux (test de performance)", 45: "Version", 46: "Directions a toute distance", 20: "Afficher les changements de mode de conduite", 19: "La touche MENU ouvre iDrive", 2: "Android Auto : Connexion...", 18: "Enregistrer les journaux sur USB", 10: "Demarrer Android Auto", 11: "Forcer l'arret", 12: "Redemarrer",
              13: "Connexion...", 14: "Ouvrir Android Auto", 15: "Demarrage automatique", 16: "A propos", 23: "Parametres", 24: "Android Auto par USB (beta)", 25: "Utiliser le GPS du telephone", 26: "Utiliser le reseau Wi-Fi Direct", 27: "Utiliser l'audio natif au lieu du Bluetooth", 28: "Commandes multimedia du volant LCI", 29: "Installer les regles PF", 30: "Verifier les regles PF", 31: "Desinstaller les regles PF", 32: "Ecriture des regles...", 33: "Regles ecrites avec succes", 34: "Regles actives", 35: "Verification des regles...", 36: "Regles en place", 37: "Regles en place, non actives", 38: "Regles absentes", 39: "Suppression des regles...", 40: "Regles supprimees avec succes", 41: "Filtre desactive, comme avant", 42: "Fait, non actif - voir journal", 43: "Echec - voir journal", 44: "Compatibilite CarPlay"},
    "it": {48: "1080p via USB", 22: "Mostra gli avvisi di iDrive", 21: "Mostra navigazione in quadro/HUD", 47: "Disattiva log (test prestazioni)", 45: "Versione", 46: "Indicazioni a qualsiasi distanza", 20: "Mostra i cambi di modalita di guida", 19: "Il tasto MENU apre iDrive", 2: "Android Auto: Connessione...", 18: "Salva i log su USB", 10: "Avvia Android Auto", 11: "Arresto forzato", 12: "Riavvia",
           13: "Connessione...", 14: "Apri Android Auto", 15: "Avvio automatico", 16: "Informazioni", 23: "Impostazioni", 24: "Android Auto via USB (beta)", 25: "Usa il GPS del telefono", 26: "Usa la rete Wi-Fi Direct", 27: "Usa l'audio nativo invece del Bluetooth", 28: "Comandi multimediali del volante LCI", 29: "Installa regole PF", 30: "Verifica regole PF", 31: "Disinstalla regole PF", 32: "Scrittura regole...", 33: "Regole scritte correttamente", 34: "Regole attive", 35: "Verifica regole...", 36: "Regole presenti", 37: "Regole presenti, non attive", 38: "Regole assenti", 39: "Rimozione regole...", 40: "Regole rimosse correttamente", 41: "Filtro disattivato, come prima", 42: "Fatto, non attivo - vedi log", 43: "Errore - vedi log", 44: "Compatibilita CarPlay"},
    "pt": {48: "1080p por USB", 22: "Mostrar avisos do iDrive", 21: "Mostrar navegacao no painel/HUD", 47: "Desativar registo (teste de desempenho)", 45: "Versao", 46: "Indicacoes a qualquer distancia", 20: "Mostrar mudancas de modo de conducao", 19: "A tecla MENU abre o iDrive", 2: "Android Auto: A ligar...", 18: "Guardar registos em USB", 10: "Iniciar Android Auto", 11: "Forcar paragem", 12: "Reiniciar",
           13: "A ligar...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Sobre", 23: "Definicoes", 24: "Android Auto por USB (beta)", 25: "Usar o GPS do telefone", 26: "Usar a rede Wi-Fi Direct", 27: "Usar audio nativo em vez de Bluetooth", 28: "Controlos multimedia do volante LCI", 29: "Instalar regras PF", 30: "Verificar regras PF", 31: "Desinstalar regras PF", 32: "A escrever regras...", 33: "Regras escritas com sucesso", 34: "Regras ativas", 35: "A verificar regras...", 36: "Regras instaladas", 37: "Regras instaladas, nao ativas", 38: "Regras nao instaladas", 39: "A remover regras...", 40: "Regras removidas com sucesso", 41: "Filtro desligado, como antes", 42: "Feito, nao ativo - ver registo", 43: "Falhou - ver registo", 44: "Compatibilidade CarPlay"},
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

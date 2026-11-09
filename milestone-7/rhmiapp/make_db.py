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
         21: "Turn arrows in cluster/HUD",   # 21 = cluster/HUD arrows checkbox (kombi_nav)
         22: "Show iDrive warnings",   # 22 = iDrive warnings checkbox (rhmiapp-32)
         23: "Settings",   # 23 = Settings row (hmiState 10) / page title (hmiState 14), rhmiapp-39
         24: "Android Auto over USB (beta)",   # 24 = USB checkbox on the Settings page (rhmiapp-40)
         25: "Use Phone GPS",   # 25 = phone GPS checkbox on the Settings page (rhmiapp-44)
         26: "Use WiFi Direct Network",   # 26 = Wi-Fi Direct only checkbox on the Settings page (rhmiapp-44)
         27: "Use Native Audio instead of Bluetooth",   # 27 = browser audio checkbox (rhmiapp-45)
         28: "Media Controls for LCI Steering Wheels",   # 28 = steering-wheel media keys checkbox (rhmiapp-45)
         29: "Firewall rules for Android Auto (pf_aa)",   # 29 = pf_aa install / uninstall checkbox (rhmiapp-47)
         17: ABOUT}
# Per-language overrides (ASCII only, like the language names above).
LANG_TEXTS = {
    "de": {22: "iDrive-Warnungen anzeigen", 21: "Abbiegepfeile in Kombi/HUD", 20: "Fahrmodus-Wechsel anzeigen", 19: "MENU-Taste öffnet iDrive", 2: "Android Auto: Verbinden...", 18: "Protokolle auf USB speichern", 10: "Android Auto starten", 11: "Beenden erzwingen", 12: "Neu starten",
           13: "Verbinden...", 14: "Android Auto öffnen", 15: "Automatisch starten", 16: "Info", 23: "Einstellungen", 24: "Android Auto über USB (Beta)", 25: "Telefon-GPS verwenden", 26: "WLAN-Direct-Netz verwenden", 27: "Natives Audio statt Bluetooth", 28: "Medientasten am LCI-Lenkrad", 29: "Firewall-Regeln für Android Auto (pf_aa)"},
    "es-ES": {22: "Mostrar avisos de iDrive", 21: "Flechas de giro en cuadro/HUD", 20: "Mostrar cambios de modo de conduccion", 19: "La tecla MENU abre iDrive", 2: "Android Auto: Conectando...", 18: "Guardar registros en USB", 10: "Iniciar Android Auto", 11: "Forzar cierre", 12: "Reiniciar",
              13: "Conectando...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Acerca de", 23: "Ajustes", 24: "Android Auto por USB (beta)", 25: "Usar el GPS del telefono", 26: "Usar la red Wi-Fi Direct", 27: "Usar audio nativo en lugar de Bluetooth", 28: "Controles multimedia del volante LCI", 29: "Reglas de firewall para Android Auto (pf_aa)"},
    "fr-FR": {22: "Afficher les avertissements iDrive", 21: "Fleches de direction combine/HUD", 20: "Afficher les changements de mode de conduite", 19: "La touche MENU ouvre iDrive", 2: "Android Auto : Connexion...", 18: "Enregistrer les journaux sur USB", 10: "Demarrer Android Auto", 11: "Forcer l'arret", 12: "Redemarrer",
              13: "Connexion...", 14: "Ouvrir Android Auto", 15: "Demarrage automatique", 16: "A propos", 23: "Parametres", 24: "Android Auto par USB (beta)", 25: "Utiliser le GPS du telephone", 26: "Utiliser le reseau Wi-Fi Direct", 27: "Utiliser l'audio natif au lieu du Bluetooth", 28: "Commandes multimedia du volant LCI", 29: "Regles de pare-feu pour Android Auto (pf_aa)"},
    "it": {22: "Mostra gli avvisi di iDrive", 21: "Frecce di svolta nel quadro/HUD", 20: "Mostra i cambi di modalita di guida", 19: "Il tasto MENU apre iDrive", 2: "Android Auto: Connessione...", 18: "Salva i log su USB", 10: "Avvia Android Auto", 11: "Arresto forzato", 12: "Riavvia",
           13: "Connessione...", 14: "Apri Android Auto", 15: "Avvio automatico", 16: "Informazioni", 23: "Impostazioni", 24: "Android Auto via USB (beta)", 25: "Usa il GPS del telefono", 26: "Usa la rete Wi-Fi Direct", 27: "Usa l'audio nativo invece del Bluetooth", 28: "Comandi multimediali del volante LCI", 29: "Regole firewall per Android Auto (pf_aa)"},
    "pt": {22: "Mostrar avisos do iDrive", 21: "Setas de viragem no painel/HUD", 20: "Mostrar mudancas de modo de conducao", 19: "A tecla MENU abre o iDrive", 2: "Android Auto: A ligar...", 18: "Guardar registos em USB", 10: "Iniciar Android Auto", 11: "Forcar paragem", 12: "Reiniciar",
           13: "A ligar...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Sobre", 23: "Definicoes", 24: "Android Auto por USB (beta)", 25: "Usar o GPS do telefone", 26: "Usar a rede Wi-Fi Direct", 27: "Usar audio nativo em vez de Bluetooth", 28: "Controlos multimedia do volante LCI", 29: "Regras de firewall para Android Auto (pf_aa)"},
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

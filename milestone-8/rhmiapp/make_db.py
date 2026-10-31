#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Builds NbtAad_ALL_Texts.zip (<lang>.txt with "id=text" lines, same format as BMW's PluginsCoded apps)
# and NbtAad_ALL_Images.zip (<imageId>.png).
import os, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Espanol", "es-US": "Espanol (US)",
         "fr-FR": "Francais", "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian"}
ABOUT = ("NBT-AAD: native wireless Android Auto\\nDeveloped by nanaisu\\n"
         "With contributions from @houssems and @James9436\\n"
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
         17: ABOUT}
# Per-language overrides (ASCII only, like the language names above).
LANG_TEXTS = {
    "de": {20: "Fahrmodus-Wechsel anzeigen", 19: "MENU-Taste oeffnet iDrive", 2: "Android Auto: Verbinden...", 18: "Protokolle auf USB speichern", 10: "Android Auto starten", 11: "Beenden erzwingen", 12: "Neu starten",
           13: "Verbinden...", 14: "Android Auto oeffnen", 15: "Automatisch starten", 16: "Info"},
    "es-ES": {20: "Mostrar cambios de modo de conduccion", 19: "La tecla MENU abre iDrive", 2: "Android Auto: Conectando...", 18: "Guardar registros en USB", 10: "Iniciar Android Auto", 11: "Forzar cierre", 12: "Reiniciar",
              13: "Conectando...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Acerca de"},
    "fr-FR": {20: "Afficher les changements de mode de conduite", 19: "La touche MENU ouvre iDrive", 2: "Android Auto : Connexion...", 18: "Enregistrer les journaux sur USB", 10: "Demarrer Android Auto", 11: "Forcer l'arret", 12: "Redemarrer",
              13: "Connexion...", 14: "Ouvrir Android Auto", 15: "Demarrage automatique", 16: "A propos"},
    "it": {20: "Mostra i cambi di modalita di guida", 19: "Il tasto MENU apre iDrive", 2: "Android Auto: Connessione...", 18: "Salva i log su USB", 10: "Avvia Android Auto", 11: "Arresto forzato", 12: "Riavvia",
           13: "Connessione...", 14: "Apri Android Auto", 15: "Avvio automatico", 16: "Informazioni"},
    "pt": {20: "Mostrar mudancas de modo de conducao", 19: "A tecla MENU abre o iDrive", 2: "Android Auto: A ligar...", 18: "Guardar registos em USB", 10: "Iniciar Android Auto", 11: "Forcar paragem", 12: "Reiniciar",
           13: "A ligar...", 14: "Abrir Android Auto", 15: "Iniciar automaticamente", 16: "Sobre"},
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

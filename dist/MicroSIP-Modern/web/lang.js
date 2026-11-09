/*
 * MicroSIP Modern - traducciones de la interfaz.
 * El espanol es el idioma fuente; las claves se normalizan sin acentos.
 */
(function () {
  const LANGS = ["es", "en", "ca", "pt", "fr", "de"];
  const NAMES = {
    es: "Español",
    en: "English",
    ca: "Català",
    pt: "Português",
    fr: "Français",
    de: "Deutsch",
  };

  const DICT = {
    "ajustes": { en: "Settings", ca: "Configuració", pt: "Configurações", fr: "Paramètres", de: "Einstellungen" },
    "atajos": { en: "Shortcuts", ca: "Dreceres", pt: "Atalhos", fr: "Raccourcis", de: "Kurzwahlen" },
    "siempre visible": { en: "Always on top", ca: "Sempre visible", pt: "Sempre visível", fr: "Toujours visible", de: "Immer im Vordergrund" },
    "ver log": { en: "View log", ca: "Veure log", pt: "Ver log", fr: "Voir le journal", de: "Protokoll anzeigen" },
    "sitio web": { en: "Website", ca: "Lloc web", pt: "Site", fr: "Site web", de: "Website" },
    "acerca de": { en: "About", ca: "Quant a", pt: "Sobre", fr: "À propos", de: "Über" },
    "salir": { en: "Exit", ca: "Surt", pt: "Sair", fr: "Quitter", de: "Beenden" },
    "teclado": { en: "Keypad", ca: "Teclat", pt: "Teclado", fr: "Clavier", de: "Tastatur" },
    "registros": { en: "Call log", ca: "Registres", pt: "Registos", fr: "Journal", de: "Anrufe" },
    "contactos": { en: "Contacts", ca: "Contactes", pt: "Contactos", fr: "Contacts", de: "Kontakte" },
    "mensajes": { en: "Messages", ca: "Missatges", pt: "Mensagens", fr: "Messages", de: "Nachrichten" },
    "buscar": { en: "Search", ca: "Cerca", pt: "Pesquisar", fr: "Rechercher", de: "Suchen" },
    "menu": { en: "Menu", ca: "Menú", pt: "Menu", fr: "Menu", de: "Menü" },
    "volver": { en: "Back", ca: "Torna", pt: "Voltar", fr: "Retour", de: "Zurück" },
    "cerrar": { en: "Close", ca: "Tanca", pt: "Fechar", fr: "Fermer", de: "Schließen" },
    "llamar": { en: "Call", ca: "Truca", pt: "Ligar", fr: "Appeler", de: "Anrufen" },
    "colgar": { en: "Hang up", ca: "Penja", pt: "Desligar", fr: "Raccrocher", de: "Auflegen" },
    "contestar": { en: "Answer", ca: "Contesta", pt: "Atender", fr: "Répondre", de: "Annehmen" },
    "rechazar": { en: "Decline", ca: "Rebutja", pt: "Rejeitar", fr: "Refuser", de: "Ablehnen" },
    "retener": { en: "Hold", ca: "Retén", pt: "Espera", fr: "Attente", de: "Halten" },
    "transferir": { en: "Transfer", ca: "Transfereix", pt: "Transferir", fr: "Transférer", de: "Weiterleiten" },
    "silenciar": { en: "Mute", ca: "Silencia", pt: "Silenciar", fr: "Couper", de: "Stumm" },
    "anadir": { en: "Add", ca: "Afegeix", pt: "Adicionar", fr: "Ajouter", de: "Hinzufügen" },
    "audio": { en: "Audio", ca: "Àudio", pt: "Áudio", fr: "Audio", de: "Audio" },
    "grabar": { en: "Record", ca: "Grava", pt: "Gravar", fr: "Enregistrer", de: "Aufnehmen" },
    "microfono": { en: "Microphone", ca: "Micròfon", pt: "Microfone", fr: "Microphone", de: "Mikrofon" },
    "no molestar": { en: "Do not disturb", ca: "No molestar", pt: "Não incomodar", fr: "Ne pas déranger", de: "Nicht stören" },
    "buzon": { en: "Voicemail", ca: "Bústia", pt: "Voicemail", fr: "Messagerie", de: "Mailbox" },
    "buzon de voz": { en: "Voicemail", ca: "Bústia de veu", pt: "Correio de voz", fr: "Messagerie vocale", de: "Voicemail" },
    "marca un numero": { en: "Dial a number", ca: "Marca un número", pt: "Marque um número", fr: "Composez un numéro", de: "Nummer wählen" },
    "anadir atajo": { en: "Add shortcut", ca: "Afegeix drecera", pt: "Adicionar atalho", fr: "Ajouter un raccourci", de: "Kurzwahl hinzufügen" },
    "numero o extension": { en: "Number or extension", ca: "Número o extensió", pt: "Número ou extensão", fr: "Numéro ou poste", de: "Nummer oder Nebenstelle" },
    "buscar contactos, llamadas o mensajes": { en: "Search contacts, calls or messages", ca: "Cerca contactes, trucades o missatges", pt: "Pesquisar contactos, chamadas ou mensagens", fr: "Rechercher contacts, appels ou messages", de: "Kontakte, Anrufe oder Nachrichten suchen" },
    "buscar en el historial": { en: "Search call log", ca: "Cerca al registre", pt: "Pesquisar no histórico", fr: "Rechercher dans le journal", de: "Anrufliste durchsuchen" },
    "buscar contacto": { en: "Search contact", ca: "Cerca contacte", pt: "Pesquisar contacto", fr: "Rechercher un contact", de: "Kontakt suchen" },
    "escribe un mensaje": { en: "Type a message", ca: "Escriu un missatge", pt: "Escreva uma mensagem", fr: "Écrivez un message", de: "Nachricht schreiben" },
    "abrir conversacion": { en: "Open conversation", ca: "Obre conversa", pt: "Abrir conversa", fr: "Ouvrir la conversation", de: "Unterhaltung öffnen" },
    "numero": { en: "Number", ca: "Número", pt: "Número", fr: "Numéro", de: "Nummer" },
    "nombre": { en: "Name", ca: "Nom", pt: "Nome", fr: "Nom", de: "Name" },
    "informacion": { en: "Information", ca: "Informació", pt: "Informação", fr: "Informations", de: "Information" },
    "etiqueta": { en: "Label", ca: "Etiqueta", pt: "Etiqueta", fr: "Libellé", de: "Bezeichnung" },
    "hoy": { en: "Today", ca: "Avui", pt: "Hoje", fr: "Aujourd'hui", de: "Heute" },
    "ayer": { en: "Yesterday", ca: "Ahir", pt: "Ontem", fr: "Hier", de: "Gestern" },
    "perdida": { en: "Missed", ca: "Perduda", pt: "Perdida", fr: "Manqué", de: "Verpasst" },
    "entrante": { en: "Incoming", ca: "Entrant", pt: "Recebida", fr: "Entrant", de: "Eingehend" },
    "saliente": { en: "Outgoing", ca: "Sortint", pt: "Efetuada", fr: "Sortant", de: "Ausgehend" },
    "todavia no hay llamadas": { en: "No calls yet", ca: "Encara no hi ha trucades", pt: "Ainda não há chamadas", fr: "Aucun appel pour l'instant", de: "Noch keine Anrufe" },
    "exportar": { en: "Export", ca: "Exporta", pt: "Exportar", fr: "Exporter", de: "Exportieren" },
    "importar": { en: "Import", ca: "Importa", pt: "Importar", fr: "Importer", de: "Importieren" },
    "vaciar": { en: "Clear", ca: "Buidar", pt: "Limpar", fr: "Vider", de: "Leeren" },
    "en llamada": { en: "In call", ca: "En trucada", pt: "Em chamada", fr: "En appel", de: "Im Gespräch" },
    "en espera": { en: "On hold", ca: "En espera", pt: "Em espera", fr: "En attente", de: "In Warteschleife" },
    "conferencia": { en: "Conference", ca: "Conferència", pt: "Conferência", fr: "Conférence", de: "Konferenz" },
    "llamando…": { en: "Calling…", ca: "Trucant…", pt: "A chamar…", fr: "Appel en cours…", de: "Wird angerufen…" },
    "consulta en curso": { en: "Consultation in progress", ca: "Consulta en curs", pt: "Consulta em curso", fr: "Consultation en cours", de: "Rücksprache läuft" },
    "completar": { en: "Complete", ca: "Completa", pt: "Concluir", fr: "Terminer", de: "Abschließen" },
    "cancelar": { en: "Cancel", ca: "Cancel·la", pt: "Cancelar", fr: "Annuler", de: "Abbrechen" },
    "unir en conferencia": { en: "Merge into conference", ca: "Uneix en conferència", pt: "Juntar em conferência", fr: "Fusionner en conférence", de: "In Konferenz zusammenführen" },
    "anadir a conferencia": { en: "Add to conference", ca: "Afegeix a conferència", pt: "Adicionar à conferência", fr: "Ajouter à la conférence", de: "Zur Konferenz hinzufügen" },
    "ciega": { en: "Blind", ca: "Cega", pt: "Cega", fr: "Aveugle", de: "Blind" },
    "con consulta": { en: "Attended", ca: "Amb consulta", pt: "Com consulta", fr: "Avec consultation", de: "Mit Rücksprache" },
    "grabando": { en: "Recording", ca: "Gravant", pt: "A gravar", fr: "Enregistrement", de: "Aufnahme" },
    "sin audio": { en: "No audio", ca: "Sense àudio", pt: "Sem áudio", fr: "Sans audio", de: "Kein Ton" },
    "favoritos": { en: "Favorites", ca: "Favorits", pt: "Favoritos", fr: "Favoris", de: "Favoriten" },
    "+ nuevo contacto": { en: "+ New contact", ca: "+ Nou contacte", pt: "+ Novo contacto", fr: "+ Nouveau contact", de: "+ Neuer Kontakt" },
    "suscribir presencia": { en: "Subscribe to presence", ca: "Subscriu presència", pt: "Subscrever presença", fr: "S'abonner à la présence", de: "Präsenz abonnieren" },
    "editar": { en: "Edit", ca: "Edita", pt: "Editar", fr: "Modifier", de: "Bearbeiten" },
    "recoger llamada": { en: "Pick up call", ca: "Recull trucada", pt: "Atender chamada", fr: "Prendre l'appel", de: "Anruf übernehmen" },
    "enviar mensaje": { en: "Send message", ca: "Envia missatge", pt: "Enviar mensagem", fr: "Envoyer un message", de: "Nachricht senden" },
    "en linea": { en: "Online", ca: "En línia", pt: "Online", fr: "En ligne", de: "Online" },
    "ausente": { en: "Away", ca: "Absent", pt: "Ausente", fr: "Absent", de: "Abwesend" },
    "desconectado": { en: "Offline", ca: "Desconnectat", pt: "Offline", fr: "Hors ligne", de: "Offline" },
    "apariencia": { en: "Appearance", ca: "Aparença", pt: "Aparência", fr: "Apparence", de: "Darstellung" },
    "cuenta": { en: "Account", ca: "Compte", pt: "Conta", fr: "Compte", de: "Konto" },
    "red": { en: "Network", ca: "Xarxa", pt: "Rede", fr: "Réseau", de: "Netzwerk" },
    "general": { en: "General", ca: "General", pt: "Geral", fr: "Général", de: "Allgemein" },
    "llamadas": { en: "Calls", ca: "Trucades", pt: "Chamadas", fr: "Appels", de: "Anrufe" },
    "modo de color": { en: "Color mode", ca: "Mode de color", pt: "Modo de cor", fr: "Mode de couleur", de: "Farbmodus" },
    "sistema": { en: "System", ca: "Sistema", pt: "Sistema", fr: "Système", de: "System" },
    "claro": { en: "Light", ca: "Clar", pt: "Claro", fr: "Clair", de: "Hell" },
    "oscuro": { en: "Dark", ca: "Fosc", pt: "Escuro", fr: "Sombre", de: "Dunkel" },
    "color de acento": { en: "Accent color", ca: "Color d'accent", pt: "Cor de destaque", fr: "Couleur d'accent", de: "Akzentfarbe" },
    "rojo microsip": { en: "MicroSIP Red", ca: "Vermell MicroSIP", pt: "Vermelho MicroSIP", fr: "Rouge MicroSIP", de: "MicroSIP-Rot" },
    "verde": { en: "Green", ca: "Verd", pt: "Verde", fr: "Vert", de: "Grün" },
    "azul": { en: "Blue", ca: "Blau", pt: "Azul", fr: "Bleu", de: "Blau" },
    "cuenta activa": { en: "Active account", ca: "Compte activa", pt: "Conta ativa", fr: "Compte actif", de: "Aktives Konto" },
    "anadir cuenta": { en: "Add account", ca: "Afegeix compte", pt: "Adicionar conta", fr: "Ajouter un compte", de: "Konto hinzufügen" },
    "servidor": { en: "Server", ca: "Servidor", pt: "Servidor", fr: "Serveur", de: "Server" },
    "usuario": { en: "Username", ca: "Usuari", pt: "Utilizador", fr: "Utilisateur", de: "Benutzer" },
    "contrasena": { en: "Password", ca: "Contrasenya", pt: "Palavra-passe", fr: "Mot de passe", de: "Passwort" },
    "nombre visible": { en: "Display name", ca: "Nom visible", pt: "Nome visível", fr: "Nom affiché", de: "Anzeigename" },
    "dominio": { en: "Domain", ca: "Domini", pt: "Domínio", fr: "Domaine", de: "Domäne" },
    "proxy": { en: "Proxy", ca: "Proxy", pt: "Proxy", fr: "Proxy", de: "Proxy" },
    "auth id": { en: "Auth ID", ca: "Auth ID", pt: "Auth ID", fr: "Auth ID", de: "Auth ID" },
    "prefijo de marcacion": { en: "Dialing prefix", ca: "Prefix de marcació", pt: "Prefixo de marcação", fr: "Préfixe d'appel", de: "Amtskennzahl" },
    "plan de marcacion": { en: "Dial plan", ca: "Pla de marcació", pt: "Plano de marcação", fr: "Plan de numérotation", de: "Wählplan" },
    "ocultar caller id": { en: "Hide caller ID", ca: "Amaga Caller ID", pt: "Ocultar Caller ID", fr: "Masquer l'ID d'appelant", de: "Anrufer-ID verbergen" },
    "transporte": { en: "Transport", ca: "Transport", pt: "Transporte", fr: "Transport", de: "Transport" },
    "srtp": { en: "SRTP", ca: "SRTP", pt: "SRTP", fr: "SRTP", de: "SRTP" },
    "direccion publica": { en: "Public address", ca: "Adreça pública", pt: "Endereço público", fr: "Adresse publique", de: "Öffentliche Adresse" },
    "registro cada (s)": { en: "Register every (s)", ca: "Registre cada (s)", pt: "Registo a cada (s)", fr: "Enregistrement toutes les (s)", de: "Registrierung alle (s)" },
    "keepalive (s)": { en: "Keepalive (s)", ca: "Keepalive (s)", pt: "Keepalive (s)", fr: "Keepalive (s)", de: "Keepalive (s)" },
    "permitir rewrite": { en: "Allow rewrite", ca: "Permet rewrite", pt: "Permitir rewrite", fr: "Autoriser la réécriture", de: "Rewrite erlauben" },
    "session timer": { en: "Session timer", ca: "Session timer", pt: "Session timer", fr: "Minuteur de session", de: "Sitzungstimer" },
    "publish al servidor": { en: "Publish to server", ca: "Publica al servidor", pt: "Publicar no servidor", fr: "Publier sur le serveur", de: "Auf Server veröffentlichen" },
    "guardar y registrar": { en: "Save and register", ca: "Desa i registra", pt: "Guardar e registar", fr: "Enregistrer et s'inscrire", de: "Speichern und registrieren" },
    "importar cuentas de microsip": { en: "Import MicroSIP accounts", ca: "Importa comptes de MicroSIP", pt: "Importar contas do MicroSIP", fr: "Importer les comptes MicroSIP", de: "MicroSIP-Konten importieren" },
    "predeterminado": { en: "Default", ca: "Predeterminat", pt: "Predefinido", fr: "Par défaut", de: "Standard" },
    "opcional": { en: "Optional", ca: "Opcional", pt: "Opcional", fr: "Facultatif", de: "Optional" },
    "obligatorio": { en: "Required", ca: "Obligatori", pt: "Obrigatório", fr: "Obligatoire", de: "Erforderlich" },
    "volumen de salida": { en: "Output volume", ca: "Volum de sortida", pt: "Volume de saída", fr: "Volume de sortie", de: "Ausgangslautstärke" },
    "volumen del microfono": { en: "Microphone volume", ca: "Volum del micròfon", pt: "Volume do microfone", fr: "Volume du micro", de: "Mikrofonlautstärke" },
    "altavoces": { en: "Speakers", ca: "Altaveus", pt: "Altifalantes", fr: "Haut-parleurs", de: "Lautsprecher" },
    "cancelacion de eco": { en: "Echo cancellation", ca: "Cancel·lació d'eco", pt: "Cancelamento de eco", fr: "Annulation d'écho", de: "Echounterdrückung" },
    "deteccion de voz (vad)": { en: "Voice detection (VAD)", ca: "Detecció de veu (VAD)", pt: "Deteção de voz (VAD)", fr: "Détection de voix (VAD)", de: "Sprachaktivierung (VAD)" },
    "tono al marcar": { en: "Dial tone", ca: "To al marcar", pt: "Tom ao marcar", fr: "Tonalité à la numérotation", de: "Wählton" },
    "codecs habilitados": { en: "Enabled codecs", ca: "Còdecs habilitats", pt: "Codecs ativados", fr: "Codecs activés", de: "Aktivierte Codecs" },
    "forzar codec": { en: "Force codec", ca: "Força còdec", pt: "Forçar codec", fr: "Forcer le codec", de: "Codec erzwingen" },
    "metodo dtmf": { en: "DTMF method", ca: "Mètode DTMF", pt: "Método DTMF", fr: "Méthode DTMF", de: "DTMF-Methode" },
    "automatico": { en: "Automatic", ca: "Automàtic", pt: "Automático", fr: "Automatique", de: "Automatisch" },
    "se aplica al reiniciar": { en: "Applies on restart", ca: "S'aplica en reiniciar", pt: "Aplica ao reiniciar", fr: "Appliqué au redémarrage", de: "Wird beim Neustart angewendet" },
    "puerto local": { en: "Local port", ca: "Port local", pt: "Porto local", fr: "Port local", de: "Lokaler Port" },
    "usar stun": { en: "Use STUN", ca: "Usa STUN", pt: "Usar STUN", fr: "Utiliser STUN", de: "STUN verwenden" },
    "rtp minimo": { en: "RTP minimum", ca: "RTP mínim", pt: "RTP mínimo", fr: "RTP minimum", de: "RTP Minimum" },
    "rtp maximo": { en: "RTP maximum", ca: "RTP màxim", pt: "RTP máximo", fr: "RTP maximum", de: "RTP Maximum" },
    "dns srv": { en: "DNS SRV", ca: "DNS SRV", pt: "DNS SRV", fr: "DNS SRV", de: "DNS SRV" },
    "nivel de log": { en: "Log level", ca: "Nivell de log", pt: "Nível de log", fr: "Niveau de journal", de: "Protokollstufe" },
    "iniciar minimizado": { en: "Start minimized", ca: "Inicia minimitzat", pt: "Iniciar minimizado", fr: "Démarrer réduit", de: "Minimiert starten" },
    "instancia unica": { en: "Single instance", ca: "Instància única", pt: "Instância única", fr: "Instance unique", de: "Einzelinstanz" },
    "idioma": { en: "Language", ca: "Idioma", pt: "Idioma", fr: "Langue", de: "Sprache" },
    "al abrir la aplicacion": { en: "When opening the app", ca: "En obrir l'aplicació", pt: "Ao abrir a aplicação", fr: "À l'ouverture de l'application", de: "Beim Öffnen der App" },
    "no abrir dos copias": { en: "Don't open two copies", ca: "No obris dues còpies", pt: "Não abrir duas cópias", fr: "Ne pas ouvrir deux copies", de: "Nicht zwei Kopien öffnen" },
    "mantener la ventana encima del resto": { en: "Keep the window above the rest", ca: "Mantén la finestra damunt la resta", pt: "Manter a janela por cima", fr: "Garder la fenêtre au-dessus", de: "Fenster im Vordergrund halten" },
    "sistema, claro u oscuro": { en: "System, light or dark", ca: "Sistema, clar o fosc", pt: "Sistema, claro ou escuro", fr: "Système, clair ou sombre", de: "System, hell oder dunkel" },
    "sin cuenta configurada": { en: "No account configured", ca: "Sense compte configurada", pt: "Sem conta configurada", fr: "Aucun compte configuré", de: "Kein Konto konfiguriert" },
    "version 1.0.0 · gpl v2": { en: "Version 1.0.0 · GPL v2", ca: "Versió 1.0.0 · GPL v2", pt: "Versão 1.0.0 · GPL v2", fr: "Version 1.0.0 · GPL v2", de: "Version 1.0.0 · GPL v2" },
    "contestar automaticamente": { en: "Auto answer", ca: "Contesta automàticament", pt: "Atender automaticamente", fr: "Réponse automatique", de: "Automatisch annehmen" },
    "responde las llamadas entrantes": { en: "Answers incoming calls", ca: "Respon les trucades entrants", pt: "Atende chamadas recebidas", fr: "Répond aux appels entrants", de: "Nimmt eingehende Anrufe an" },
    "timbre": { en: "Ringtone", ca: "Timbre", pt: "Toque", fr: "Sonnerie", de: "Klingelton" },
    "volumen del timbre": { en: "Ringtone volume", ca: "Volum del timbre", pt: "Volume do toque", fr: "Volume de la sonnerie", de: "Klingeltonlautstärke" },
    "dispositivo de timbre": { en: "Ringtone device", ca: "Dispositiu del timbre", pt: "Dispositivo de toque", fr: "Périphérique de sonnerie", de: "Klingelton-Gerät" },
    "llamada en espera": { en: "Call waiting", ca: "Trucada en espera", pt: "Chamada em espera", fr: "Appel en attente", de: "Anklopfen" },
    "permite una segunda llamada": { en: "Allows a second call", ca: "Permet una segona trucada", pt: "Permite uma segunda chamada", fr: "Autorise un deuxième appel", de: "Erlaubt einen zweiten Anruf" },
    "auriculares jabra": { en: "Jabra headset", ca: "Auriculars Jabra", pt: "Auscultadores Jabra", fr: "Casque Jabra", de: "Jabra-Headset" },
    "boton del auricular usb": { en: "USB headset button", ca: "Botó de l'auricular USB", pt: "Botão do auscultador USB", fr: "Bouton du casque USB", de: "USB-Headset-Taste" },
  "estado de microsoft teams": { en: "Microsoft Teams status", ca: "Estat de Microsoft Teams", pt: "Estado do Microsoft Teams", fr: "Statut de Microsoft Teams", de: "Microsoft Teams-Status" },
  "notificacion de llamada": { en: "Call notification", ca: "Notificació de trucada", pt: "Notificação de chamada", fr: "Notification d'appel", de: "Anrufbenachrichtigung" },
  "aviso con contestar y colgar si la app no esta delante": { en: "Popup with Answer and Hang up when the app is not in front", ca: "Avís amb Contesta i Penja si l'app no és davant", pt: "Aviso com Atender e Desligar se a app não estiver à frente", fr: "Avis avec Répondre et Raccrocher si l'app n'est pas au premier plan", de: "Hinweis mit Annehmen und Auflegen, wenn die App nicht im Vordergrund ist" },
  "posicion del aviso": { en: "Popup position", ca: "Posició de l'avís", pt: "Posição do aviso", fr: "Position de la notification", de: "Position des Hinweises" },
  "arriba a la derecha": { en: "Top right", ca: "A dalt a la dreta", pt: "Cima à direita", fr: "En haut à droite", de: "Oben rechts" },
  "arriba a la izquierda": { en: "Top left", ca: "A dalt a l'esquerra", pt: "Cima à esquerda", fr: "En haut à gauche", de: "Oben links" },
  "abajo a la derecha": { en: "Bottom right", ca: "A baix a la dreta", pt: "Baixo à direita", fr: "En bas à droite", de: "Unten rechts" },
  "abajo a la izquierda": { en: "Bottom left", ca: "A baix a l'esquerra", pt: "Baixo à esquerda", fr: "En bas à gauche", de: "Unten links" },
  "pantalla del aviso": { en: "Popup screen", ca: "Pantalla de l'avís", pt: "Ecrã do aviso", fr: "Écran de la notification", de: "Bildschirm des Hinweises" },
  "pantalla principal": { en: "Primary screen", ca: "Pantalla principal", pt: "Ecrã principal", fr: "Écran principal", de: "Hauptbildschirm" },
  "pantalla de microsip": { en: "MicroSIP screen", ca: "Pantalla del MicroSIP", pt: "Ecrã do MicroSIP", fr: "Écran de MicroSIP", de: "MicroSIP-Bildschirm" },
  "ocupado durante la llamada": { en: "Busy while on a call", ca: "Ocupat durant la trucada", pt: "Ocupado durante a chamada", fr: "Occupé pendant l'appel", de: "Beschäftigt während des Anrufs" },
    "contesta, silencia y cuelga con el boton central": { en: "Answer, mute and hang up with the center button", ca: "Contesta, silencia i penja amb el botó central", pt: "Atende, silencia e desliga com o botão central", fr: "Répondre, couper et raccrocher avec le bouton central", de: "Annehmen, stummschalten und auflegen mit der mittleren Taste" },
    "sdk oficial · busylight activa": { en: "Official SDK · busylight active", ca: "SDK oficial · busylight activa", pt: "SDK oficial · busylight ativa", fr: "SDK officiel · busylight active", de: "Offizielles SDK · Busylight aktiv" },
    "desviar llamadas a": { en: "Forward calls to", ca: "Desvia trucades a", pt: "Encaminhar chamadas para", fr: "Renvoyer les appels vers", de: "Anrufe weiterleiten an" },
    "desvio activo": { en: "Forwarding active", ca: "Desviament actiu", pt: "Encaminhamento ativo", fr: "Renvoi actif", de: "Weiterleitung aktiv" },
    "redirige todas las llamadas": { en: "Redirects all calls", ca: "Redirigeix totes les trucades", pt: "Redireciona todas as chamadas", fr: "Redirige tous les appels", de: "Leitet alle Anrufe um" },
    "codigo de recogida": { en: "Pickup code", ca: "Codi de recollida", pt: "Código de atendimento", fr: "Code de prise d'appel", de: "Abholcode" },
    "probar": { en: "Test", ca: "Prova", pt: "Testar", fr: "Tester", de: "Testen" },
    "probar timbre": { en: "Test ringtone", ca: "Prova el timbre", pt: "Testar toque", fr: "Tester la sonnerie", de: "Klingelton testen" },
    "ringtone.wav junto a la aplicacion": { en: "ringtone.wav next to the app", ca: "ringtone.wav al costat de l'aplicació", pt: "ringtone.wav junto à aplicação", fr: "ringtone.wav à côté de l'application", de: "ringtone.wav neben der App" },
    "rechaza llamadas entrantes": { en: "Rejects incoming calls", ca: "Rebutja trucades entrants", pt: "Rejeita chamadas recebidas", fr: "Refuse les appels entrants", de: "Weist eingehende Anrufe ab" },
    "atajos de marcacion": { en: "Speed dials", ca: "Dreceres de marcació", pt: "Atalhos de marcação", fr: "Raccourcis d'appel", de: "Kurzwahlen" },
    "guardar atajo": { en: "Save shortcut", ca: "Desa drecera", pt: "Guardar atalho", fr: "Enregistrer le raccourci", de: "Kurzwahl speichern" },
    "eliminar": { en: "Delete", ca: "Elimina", pt: "Eliminar", fr: "Supprimer", de: "Löschen" },
    "tu:": { en: "You:", ca: "Tu:", pt: "Você:", fr: "Vous :", de: "Sie:" },
    "no enviado": { en: "Not sent", ca: "No enviat", pt: "Não enviado", fr: "Non envoyé", de: "Nicht gesendet" },
    "todavia no hay conversaciones": { en: "No conversations yet", ca: "Encara no hi ha converses", pt: "Ainda não há conversas", fr: "Aucune conversation", de: "Noch keine Unterhaltungen" },
    "sin mensajes con este numero": { en: "No messages with this number", ca: "Sense missatges amb aquest número", pt: "Sem mensagens com este número", fr: "Aucun message avec ce numéro", de: "Keine Nachrichten mit dieser Nummer" },
    "registrado": { en: "Registered", ca: "Registrat", pt: "Registado", fr: "Enregistré", de: "Registriert" },
    "silenciado": { en: "Muted", ca: "Silenciat", pt: "Silenciado", fr: "Coupé", de: "Stumm" },
    "enviado": { en: "Sent", ca: "Enviat", pt: "Enviado", fr: "Envoyé", de: "Gesendet" },
    "sin contactos. usa “nuevo contacto”.": { en: "No contacts. Use “New contact”.", ca: "Sense contactes. Fes servir “Nou contacte”.", pt: "Sem contactos. Use “Novo contacto”.", fr: "Aucun contact. Utilisez « Nouveau contact ».", de: "Keine Kontakte. „Neuer Kontakt“ verwenden." },
    "sin atajos. anade uno arriba.": { en: "No shortcuts. Add one above.", ca: "Sense dreceres. Afegeix-ne una a dalt.", pt: "Sem atalhos. Adicione um acima.", fr: "Aucun raccourci. Ajoutez-en un ci-dessus.", de: "Keine Kurzwahlen. Oben eine hinzufügen." },
    "sin registrar": { en: "Not registered", ca: "Sense registrar", pt: "Sem registar", fr: "Non enregistré", de: "Nicht registriert" },
    "falta la contrasena": { en: "Password missing", ca: "Falta la contrasenya", pt: "Falta a palavra-passe", fr: "Mot de passe manquant", de: "Passwort fehlt" },
    "interfaz webview2 sobre el motor c++ de microsip (pjsua), con integracion de auriculares jabra.": { en: "WebView2 interface on MicroSIP's C++ engine (pjsua), with Jabra headset integration.", ca: "Interfície WebView2 sobre el motor C++ de MicroSIP (pjsua), amb integració d'auriculars Jabra.", pt: "Interface WebView2 sobre o motor C++ do MicroSIP (pjsua), com integração de auscultadores Jabra.", fr: "Interface WebView2 sur le moteur C++ de MicroSIP (pjsua), avec intégration du casque Jabra.", de: "WebView2-Oberfläche auf der C++-Engine von MicroSIP (pjsua), mit Jabra-Headset-Integration." },
    "webview2 + html/css/js": { en: "WebView2 + HTML/CSS/JS", ca: "WebView2 + HTML/CSS/JS", pt: "WebView2 + HTML/CSS/JS", fr: "WebView2 + HTML/CSS/JS", de: "WebView2 + HTML/CSS/JS" },
    "soft": { en: "Soft", ca: "Soft", pt: "Soft", fr: "Soft", de: "Soft" },
    "hard": { en: "Hard", ca: "Hard", pt: "Hard", fr: "Hard", de: "Hard" },
    "0 – silencio": { en: "0 – silent", ca: "0 – silenci", pt: "0 – silêncio", fr: "0 – silence", de: "0 – still" },
    "1 – errores": { en: "1 – errors", ca: "1 – errors", pt: "1 – erros", fr: "1 – erreurs", de: "1 – Fehler" },
    "2 – info": { en: "2 – info", ca: "2 – info", pt: "2 – info", fr: "2 – info", de: "2 – Info" },
    "3 – detalle": { en: "3 – detail", ca: "3 – detall", pt: "3 – detalhe", fr: "3 – détail", de: "3 – Detail" },
    "4 – debug": { en: "4 – debug", ca: "4 – debug", pt: "4 – debug", fr: "4 – debug", de: "4 – Debug" },
    "5 – trace": { en: "5 – trace", ca: "5 – trace", pt: "5 – trace", fr: "5 – trace", de: "5 – Trace" },
  };

  let current = "es";
  let observer = null;

  function norm(text) {
    return String(text)
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .replace(/\s+/g, " ")
      .trim()
      .toLowerCase();
  }

  function t(text) {
    if (current === "es") return text;
    const entry = DICT[norm(text)];
    if (!entry) return text;
    return entry[current] || entry.en || text;
  }

  function translateTextNode(node) {
    const raw = node.nodeValue;
    const key = norm(raw);
    if (!key) return;
    const translated = t(raw);
    if (translated !== raw.trim()) {
      const leading = (raw.match(/^\s*/) || [""])[0];
      const trailing = (raw.match(/\s*$/) || [""])[0];
      node.nodeValue = leading + translated + trailing;
    }
  }

  function translateTree(root) {
    if (!root) return;
    if (root.nodeType === 3) {
      translateTextNode(root);
      return;
    }
    if (root.nodeType !== 1) return;
    ["placeholder", "title", "aria-label"].forEach((attr) => {
      const value = root.getAttribute(attr);
      if (value) {
        const translated = t(value);
        if (translated !== value.trim()) root.setAttribute(attr, translated);
      }
    });
    for (const child of root.childNodes) translateTree(child);
  }

  function apply() {
    translateTree(document.body);
  }

  function setLanguage(lang) {
    current = LANGS.includes(lang) ? lang : detectSystem();
    document.documentElement.setAttribute("lang", current);
    apply();
  }

  function detectSystem() {
    const lang = String(navigator.language || "es").toLowerCase();
    if (lang.startsWith("ca")) return "ca";
    if (lang.startsWith("pt")) return "pt";
    if (lang.startsWith("fr")) return "fr";
    if (lang.startsWith("de")) return "de";
    if (lang.startsWith("en")) return "en";
    if (lang.startsWith("es")) return "es";
    return "en";
  }

  function start(lang) {
    setLanguage(lang || detectSystem());
    if (observer || !document.body) return;
    observer = new MutationObserver((mutations) => {
      for (const mutation of mutations) {
        if (mutation.type === "childList") {
          mutation.addedNodes.forEach((node) => translateTree(node));
        }
      }
    });
    observer.observe(document.body, {
      childList: true,
      subtree: true,
    });
  }

  window.i18n = {
    t,
    start,
    apply,
    setLanguage,
    detectSystem,
    languages: LANGS,
    names: NAMES,
    get current() {
      return current;
    },
  };
  window.t = t;
})();

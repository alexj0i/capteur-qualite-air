# Historique des versions

## 2.2
- Écran sur un bus I2C séparé (D6/D7) par défaut.
- Page web : graphique adapté à la largeur de l'écran, correction de l'affichage de « µm ».

## 2.1
- Correction : le capteur n'était jamais retrouvé si sa première détection échouait (la bibliothèque Adafruit PM25 AQI 2.x n'accepte qu'un appel à `begin_I2C()`).
- Message « capteur absent » limité à un par cycle, avec la cause probable.

## 2.0
- Configuration Wi-Fi par portail (WiFiManager), réglages enregistrés en flash (LittleFS).
- Page web locale avec historique 24 h, paramètres et API JSON ; nom `capteur-air.local`.
- Mise à jour par Wi-Fi (OTA), heure par NTP, extinction de l'écran la nuit (option).
- Nouvelle interface OLED : barre d'icônes, jauge, tendance, pages historique et informations.
- Option deux bus I2C, bouton FLASH (page suivante / portail).

## 1.x
- Mesure périodique avec mise en veille du capteur (broche SET), envoi ThingSpeak, journal série et commandes de diagnostic.

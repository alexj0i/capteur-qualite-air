# Boîtier « Atmos 77 »

Boîtier imprimé en 3D d'inspiration années 70 pour le Capteur d'air.

## Fichiers

| Fichier | Contenu |
|---|---|
| `atmos77.f3d` | Modèle Fusion 360 (source modifiable) |
| `atmos77.step` | Export STEP, pour les autres logiciels de CAO |
| `stl/` | Pièces prêtes à trancher |
| `photos/` | Photos du boîtier imprimé et assemblé |

## Contraintes de conception

- **Capteur** : environ 51 × 35,5 × 13,6 mm (carte Adafruit). L'entrée et la sortie d'air sont sur un grand côté : à plaquer contre une grille, avec un joint en mousse entre les deux ouvertures.
- **Écran** : fenêtre d'environ 22 × 11 mm pour une OLED 0,96" (à mesurer sur ton module).
- **ESP8266** : accès au port USB, et si possible au bouton FLASH (page suivante, portail Wi-Fi).
- Jeu de 0,2 à 0,3 mm par côté autour des cartes ; fentes d'aération pour l'ESP8266.

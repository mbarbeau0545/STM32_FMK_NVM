# STM32_FMK_NVM

Documentation du module dans Doc/FMKNVM_Architecture.md

Le dossier `Templates` contient les modèles utilisés pour générer ou créer
une nouvelle configuration FMK_NVM :

- `FMKNVM_ConfigPublic.h.tpl` : identifiants des partitions et objets ;
- `FMKNVM_ConfigPrivate.h.tpl` : caches, descripteurs et politiques ;
- `FMKNVM_ConfigSpecific.h.tpl` : interface du mapping physique ;
- `FMKNVM_ConfigSpecific.c.tpl` : sélection du backend et des partitions ;
- `FMKNVM_Backend.h.tpl` : interface d'un backend concret ;
- `FMKNVM_Backend.c.tpl` : squelette complet d'un backend concret.

Les zones comprises entre les marqueurs
`CAUTION : Automatic generated code section` sont destinées au générateur.
Pour créer un backend, remplacer également `BackendTemplate` par le nom de la
technologie ou du composant ciblé.

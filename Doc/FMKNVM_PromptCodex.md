# Prompt maître Codex — Implémentation de FMK_NVM

Tu dois implémenter `FMK_NVM` dans ce dépôt conformément à la spécification d’architecture fournie avec cette tâche.

La version de référence est le document actuellement nommé `FMKNVM_Architecture.md`. Dans le dépôt, sa destination canonique est :

```text
src/1_FMK/FMK_HAL/FMK_NVM/Doc/FMKNVM_Architecture.md
```

Ce document est la source d’autorité fonctionnelle et architecturale de `FMK_NVM`. Ne conserve pas deux spécifications divergentes dans le dépôt : utilise la version fournie comme contenu de la documentation canonique, sauf règle contraire explicite dans `AGENTS.md`.

---

## 1. Ordre de priorité des sources

Avant toute modification :

1. Recherche et lis intégralement tous les fichiers de règles applicables :
   - `AGENTS.md` ;
   - `Agent.md` ;
   - `agents.md` ;
   - Misra Compliance 
   - tout fichier de règles référencé par ceux-ci.
2. Applique les règles selon leur portée. Une règle située dans un sous-répertoire prévaut, pour ce sous-répertoire, sur une règle plus générale.
3. Lis intégralement `FMKNVM_Architecture.md`.
4. Inspecte les définitions et implémentations réelles du dépôt pour :
   - `t_uint8`, `t_uint16`, `t_uint32`, `t_sint32`, `t_bool` ;
   - `t_eReturnCode` et toutes ses valeurs ;
   - `t_eCyclicModState` ;
   - `FMKCPU_GetTick()` ;
   - `Library/FIFO` et uniquement ses API publiques ;
   - `FMK_I2C`, `FMKI2C_Cyclic()` et son modèle IT/DMA ;
   - `FMK_SPI`, `FMKSPI_Cyclic()` et son modèle asynchrone ;
   - les utilitaires CRC existants ;
   - les autres modules FMK cycliques ;
   - les configurations `FMKCFG_ConfigPublic`, `FMKCFG_ConfigPrivate` et `FMKCFG_ConfigSpecific` ;
   - les tests, mocks et conventions de build du dépôt.

Ordre de priorité :

1. règles `AGENTS.md` applicables ;
2. architecture `FMKNVM_Architecture.md` ;
3. conventions et API existantes du dépôt ;
4. choix d’implémentation internes.

Le comportement décrit par l’architecture est obligatoire. Les noms exacts, types, codes de retour, includes, commentaires et règles de style doivent rester conformes au dépôt.

Si une exigence de l’architecture est incompatible avec une règle de dépôt ou une API réelle, n’invente pas de contournement silencieux. Arrête la phase concernée, cite les deux contraintes et propose le plus petit ajustement cohérent.

---

## 2. Contraintes non négociables

- C11.
- Aucune allocation dynamique.
- Aucun `malloc`, `calloc`, `realloc` ou `free`.
- Utiliser les types du projet lorsqu’ils existent.
- Ne pas créer un second système générique de codes de retour.
- Utiliser `t_eReturnCode` pour le résultat générique et `t_eFMKNVM_OperationStatus` pour le statut NVM détaillé.
- Aucun handle HAL dans `FMK_NVM.h`, `FMK_NVM.c` ou l’API publique.
- Aucun appel direct à `HAL_I2C`, `HAL_SPI` ou à une HAL Flash depuis le cœur `FMK_NVM.c`.
- Le backend EEPROM I²C utilise exclusivement `FMK_I2C`.
- Le backend EEPROM SPI utilise exclusivement `FMK_SPI`.
- Les détails Flash STM32 restent privés aux backends G4/H7.
- Une seule opération backend physique peut être active.
- Toutes les API `FMKNVM_*` sont interdites depuis une ISR.
- `FMK_NVM` n’est pas thread-safe et ne contient aucun mutex interne.
- Toutes les API `FMKNVM_*` appartiennent à un contexte propriétaire unique.
- En RTOS, les autres tâches transmettent leurs demandes à la tâche propriétaire ; elles n’appellent pas directement `FMKNVM_*`.
- La FIFO est manipulée uniquement avec les API publiques de `Library/FIFO`.
- La FIFO ne contient jamais une copie complète du payload.
- Les pointeurs de données fournis par l’application ne sont jamais conservés après le retour de `FMKNVM_Set()`.
- Les callbacks ne sont jamais dispatchés au milieu d’une progression interne synchrone réentrante.
- Les sources des backends matériels non sélectionnés doivent être exclues du build, pas masquées par des `#if` matériels dans le cœur.

---

## 3. Terminologie normative obligatoire

Utilise strictement les termes suivants dans le code, les commentaires, les tests et la documentation :

- **slot** : emplacement physique fixe d’une partition, contenant une zone header, une zone payload réservée et une commit unit ;
- **record** : header sérialisé, payload logique et commit correspondant à une version d’objet écrite dans un slot ;
- **erase unit** : plus petite zone Flash effaçable indépendamment, page G4 ou secteur H7 selon le backend ;
- **EEPROM page** : fenêtre d’écriture interne d’une EEPROM I²C/SPI, dont une commande d’écriture ne doit pas franchir la frontière ;
- **program unit** : granularité minimale de programmation du support ;
- **commit unit** : program unit réservée au marqueur final de commit ;
- **partition** : plage NVM fixe et indépendante réservée à un objet ;
- **payload logique** : `payloadSize` octets réellement valides et couverts par le CRC ;
- **zone payload réservée** : capacité physique fixe du slot, `slotPayloadCapacity_u32`.

Les mots « page mémoire » et « page NVM » sont interdits hors citation documentaire. Le mot « page » seul est interdit : écrire explicitement `G4 erase unit`, `H7 erase unit` ou `EEPROM page`.

Ne confonds jamais :

- rotation de slots ;
- effacement d’une erase unit ;
- découpage d’une écriture aux frontières d’EEPROM page.

---

## 4. Sémantique fondamentale des objets

`FMK_NVM` stocke des **images opaques d’octets de taille variable**.

Pour chaque objet :

- `slotPayloadCapacity_u32` est une capacité physique fixe configurée ;
- la taille logique courante est fournie à chaque `FMKNVM_Set()` ;
- toute taille comprise entre `1` et `slotPayloadCapacity_u32` est valide ;
- deux écritures successives du même objet peuvent avoir des tailles différentes ;
- une nouvelle image remplace entièrement l’image précédente ;
- aucune mise à jour partielle n’existe dans `FMK_NVM` ;
- le header persistant mémorise `payloadSize` ;
- le CRC payload couvre exactement `payloadSize` octets ;
- les octets inutilisés de la zone payload réservée ne font pas partie du payload logique.

Le cœur `FMK_NVM` ne connaît pas :

- le type C de l’objet ;
- sa structure métier ;
- sa valeur par défaut ;
- sa version de schéma métier ;
- sa migration ;
- sa validation applicative.

Ces responsabilités appartiennent à la couche appelante.

En conséquence :

- ne crée pas de callback de migration dans `FMK_NVM` ;
- ne charge pas de valeurs par défaut dans `FMKNVM_Init()` ;
- n’implémente pas `FMKNVM_RestoreDefaultsSync()` ;
- n’ajoute pas de `schemaVersion` métier dans le contrat public, sauf si le document d’architecture est explicitement modifié.

Lorsqu’aucun record valide n’existe pour un objet :

- l’objet reste en état `NO_VALID_RECORD` ;
- `FMKNVM_Get()` ne copie aucun octet ;
- la couche appelante décide éventuellement d’appeler `FMKNVM_Set()` avec une image initiale.

---

## 5. Méthode de travail obligatoire

Travaille par phases. N’implémente pas tout le module en un seul bloc non vérifié.

Après chaque phase :

1. compile la ou les variantes concernées ;
2. exécute les tests pertinents ;
3. exécute `git diff --check` ;
4. corrige toutes les erreurs introduites ;
5. indique les fichiers créés et modifiés ;
6. indique les commandes exécutées et leurs résultats exacts ;
7. indique les exigences encore non implémentées ;
8. ne modifie aucun fichier hors périmètre sans justification.

Commence par l’audit. Si aucun conflit bloquant n’est trouvé, poursuis ensuite les phases d’implémentation dans l’ordre. En cas de conflit bloquant, arrête-toi avant de modifier le code concerné.

---

## 6. Phase A — Audit du dépôt, sans modification

Ne modifie aucun fichier.

Produis un rapport comprenant :

1. Les fichiers `AGENTS.md` ou équivalents trouvés, leur emplacement et leur portée.
2. La correspondance entre les types illustratifs de l’architecture et les types réels du projet.
3. Les valeurs de `t_eReturnCode` utilisables pour :
   - succès ;
   - pending ;
   - paramètre invalide ;
   - taille invalide ;
   - configuration invalide ;
   - module non initialisé ;
   - busy ;
   - FIFO pleine ;
   - timeout ;
   - erreur backend.
4. Les signatures réelles de :
   - `FMKCPU_GetTick()` ;
   - `FMKI2C_Cyclic()` ;
   - `FMKSPI_Cyclic()` ;
   - `Library/FIFO` ;
   - l’utilitaire CRC retenu.
5. Les conventions réelles des modules FMK cycliques :
   - `Init` ;
   - `Cyclic` ;
   - `GetState` ;
   - `SetState` ;
   - nommage des variables, fonctions privées et paramètres.
6. Les commandes de build, tests et analyse définies par le dépôt.
7. Le plan exact des fichiers à créer ou modifier.
8. Les incompatibilités ou ambiguïtés bloquantes.

Ne génère aucun code pendant cette phase.

---

## 7. Phase B — Contrats publics, configuration et cache RAM

Créer ou adapter uniquement le squelette compilable :

```text
src/1_FMK/FMK_HAL/FMK_NVM/Src/FMK_NVM.h
src/1_FMK/FMK_HAL/FMK_NVM/Src/FMK_NVM.c
src/1_FMK/FMK_HAL/FMK_NVM/Src/FMKNVM_Backend.h
src/1_FMK/FMK_CFG/FMKCFG_ConfigFiles/FMKNVM_ConfigPublic.h
src/1_FMK/FMK_CFG/FMKCFG_ConfigFiles/FMKNVM_ConfigPrivate.h
src/1_FMK/FMK_CFG/FMKCFG_ConfigFiles/FMKNVM_ConfigPrivate.c
src/1_FMK/FMK_CFG/FMKCFG_ConfigSpecific/FMKNVM_ConfigSpecific.h
src/1_FMK/FMK_CFG/FMKCFG_ConfigSpecific/FMKNVM_ConfigSpecific.c
```

Respecte l’arborescence réelle du dépôt si elle diffère légèrement, sans créer une arborescence concurrente.

### API publique à implémenter

L’API fonctionnelle cible est :

```c
t_eReturnCode FMKNVM_Init(t_eFMKNVM_OperationStatus * f_Status_pe);
t_eReturnCode FMKNVM_Cyclic(t_eFMKNVM_OperationStatus * f_Status_pe);
t_eReturnCode FMKNVM_GetState(t_eCyclicModState * f_State_pe,
                               t_eFMKNVM_OperationStatus * f_Status_pe);
t_eReturnCode FMKNVM_SetState(t_eCyclicModState f_State_e,
                               t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_GetObjectInfo(
    t_eFMKNVM_ObjectId f_ObjectId_e,
    t_sFMKNVM_ObjectInfo * f_ObjectInfo_ps,
    t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_Get(
    t_eFMKNVM_ObjectId f_ObjectId_e,
    void * f_Data_pv,
    t_uint32 f_DataCapacity_u32,
    t_uint32 * f_DataSize_pu32,
    t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_Set(
    t_eFMKNVM_ObjectId f_ObjectId_e,
    const void * f_Data_pv,
    t_uint32 f_DataSize_u32,
    t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_IsDirty(
    t_eFMKNVM_ObjectId f_ObjectId_e,
    t_bool * f_IsDirty_pb,
    t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_RequestCommit(
    t_eFMKNVM_ObjectId f_ObjectId_e,
    t_cbFMKNVM_Commit * f_Callback_pcb,
    void * f_Context_pv,
    t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_CommitSync(
    t_eFMKNVM_ObjectId f_ObjectId_e,
    t_uint32 f_Timeout_u32,
    t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_CommitAllSync(
    t_uint32 f_Timeout_u32,
    t_eFMKNVM_OperationStatus * f_Status_pe);

t_eReturnCode FMKNVM_GetDiagnostics(
    t_eFMKNVM_ObjectId f_ObjectId_e,
    t_sFMKNVM_Diagnostics * f_Diagnostics_ps,
    t_eFMKNVM_OperationStatus * f_Status_pe);
```

Adapte uniquement la syntaxe aux conventions réelles du dépôt. Ne supprime pas une sémantique exigée.

### Configuration statique des objets

Chaque objet possède :

- un cache statique de `slotPayloadCapacity_u32` octets ;
- une capacité de slot propre ;
- une partition fixe ;
- un nombre de slots ;
- une politique de commit propre.

Le descripteur ne contient aucune valeur par défaut ni information de type métier.

Génère :

- un cache statique par objet ;
- `g_FMKNVM_ObjectDescriptors_as[]` dans un `.c`, jamais dans un header ;
- un snapshot actif partagé dimensionné par `FMKNVM_MAX_SLOT_PAYLOAD_CAPACITY` ;
- un buffer de lecture partagé dimensionné par le même maximum ;
- la FIFO, les callbacks, états et diagnostics statiques ;
- les `_Static_assert` nécessaires.

### Contrat de `FMKNVM_GetObjectInfo()`

Retourner :

- la capacité logique maximale de l’objet ;
- la taille courante chargée ;
- la présence ou non d’un record valide.

Ne pas exposer les offsets de partition ni les paramètres matériels.

### Contrat de `FMKNVM_Get()`

- lit uniquement le cache RAM ;
- ne déclenche aucun accès NVM ;
- si aucun record valide n’existe, ne copie rien et retourne `FMKNVM_STATUS_NO_VALID_RECORD` ;
- si le buffer est trop petit, ne copie rien ;
- renseigne toujours la taille requise lorsque le pointeur de sortie est valide ;
- en succès, copie exactement la taille logique courante ;
- aucune copie partielle.

### Contrat de `FMKNVM_Set()`

- accepte toute taille `1 <= size <= slotPayloadCapacity_u32` ;
- rejette la taille zéro ;
- rejette une taille supérieure à la capacité ;
- remplace l’image complète ;
- compare la taille et les octets avec le cache courant ;
- retourne `UNCHANGED` sans incrémenter de génération si taille et contenu sont identiques ;
- copie immédiatement les données dans le cache ;
- ne conserve jamais le pointeur appelant ;
- met à jour `cachedDataSize_u32`, `currentGeneration_u32` et `dirty_b` si l’image change.

### Tests de phase B

Couvrir au minimum :

- objets de capacités différentes ;
- taille `Set()` nulle ;
- taille `Set()` égale à la capacité ;
- taille `Set()` supérieure à la capacité ;
- deux tailles successives différentes sur le même objet ;
- données inchangées ;
- pointeurs nuls ;
- identifiant invalide ;
- `Get()` sans record valide ;
- `Get()` avec buffer insuffisant, exact et supérieur ;
- absence de copie partielle ;
- calcul et assertions des maxima.

---

## 8. Phase C — Contrat backend et backend RAM de référence

Implémente exactement le contrat privé défini dans l’architecture :

```c
typedef struct
{
    t_uint32 storageSize_u32;
    t_uint32 eraseUnitSize_u32;
    t_uint32 programUnitSize_u32;
    t_uint32 commitUnitSize_u32;
} t_sFMKNVM_BackendGeometry;

typedef struct
{
    t_bool eraseRequired_b;
    t_bool readWhileWriteSupported_b;
    t_uint32 maxTransferSize_u32;
} t_sFMKNVM_BackendCapabilities;
```

Et les opérations :

- `Init` ;
- `GetGeometry` ;
- `GetCapabilities` ;
- `Read` ;
- `Program` ;
- `Erase` ;
- `IsBusy` ;
- `GetOperationResult` ;
- `Cyclic`.

### Sémantique backend obligatoire

- une seule opération physique active ;
- `Read`, `Program` et `Erase` retournent `RC_OK` si terminés immédiatement ;
- ils retournent `RC_WARNING_PENDING` s’ils ont démarré de manière asynchrone ;
- après `PENDING`, le cœur conserve tous les buffers jusqu’à la fin ;
- le cœur attend `IsBusy() == FALSE`, puis appelle `GetOperationResult()` ;
- `GetOperationResult()` reste lisible jusqu’au démarrage de l’opération suivante ;
- une opération terminée immédiatement par `RC_OK` ne nécessite pas `GetOperationResult()` ;
- le backend reste responsable de toutes les contraintes matérielles internes ;
- le cœur peut limiter la taille d’un appel à `maxTransferSize_u32` mais ne gère jamais les frontières d’EEPROM page.

### Backend RAM de test

Créer un backend RAM persistant entre deux resets contrôleur simulés, capable de :

- lecture ;
- programmation ;
- effacement ;
- opérations immédiates ;
- opérations asynchrones ;
- coupure après N opérations ;
- coupure après N octets ;
- échec de lecture ;
- échec de programmation ;
- échec d’effacement ;
- corruption d’un octet ;
- reset du module sans effacer le stockage ;
- comptage des opérations et octets.

Aucune logique de test spécifique ne doit entrer dans le cœur de production.

---

## 9. Phase D — Format physique, scan et reconstruction

Implémente :

- sérialisation little-endian explicite ;
- désérialisation ;
- `magic` ;
- `storageFormatVersion` ;
- `headerSize` ;
- `objectId` ;
- `flags` ;
- `sequence` ;
- `payloadSize` ;
- `payloadCrc32` ;
- `headerCrc32` ;
- marqueur de commit et complément ;
- alignement header/payload/commit ;
- calcul du `slotStride` propre à chaque objet ;
- offset fixe de la commit unit fondé sur la capacité du slot, jamais sur la taille courante ;
- scan de chaque partition ;
- validation des records ;
- sélection de séquence modulo `2^32` ;
- chargement du cache et de `cachedDataSize_u32` ;
- état `NO_VALID_RECORD` lorsque nécessaire.

Le CRC payload couvre exactement `payloadSize` octets. Les octets inutilisés de la zone payload réservée ne sont pas couverts.

Valider un record uniquement si :

- le magic est correct ;
- la version de format est supportée ;
- l’ObjectId correspond à la partition ;
- `payloadSize > 0` ;
- `payloadSize <= slotPayloadCapacity_u32` ;
- le CRC header est correct ;
- le commit et son complément sont corrects ;
- le CRC des `payloadSize` octets est correct.

N’ajoute aucune migration métier et aucune valeur par défaut.

### Tests de phase D

- stockage vierge ;
- record valide ;
- plusieurs records valides ;
- header corrompu ;
- payload logique corrompu ;
- zone payload réservée inutilisée modifiée ;
- commit absent ou partiel ;
- payloadSize nul ;
- payloadSize supérieur à la capacité ;
- mauvais ObjectId ;
- format inconnu ;
- overflow de séquence ;
- objets de tailles 7, 32, 96, 255 et 512 octets ;
- écritures successives de tailles différentes ;
- calcul de slotStride avec plusieurs granularités backend.

---

## 10. Phase E — Machine transactionnelle et copy-on-write

Implémente la machine :

```text
IDLE
SELECT_SLOT
PREPARE_ERASE_UNIT
ERASE_UNIT
WRITE_HEADER
WRITE_PAYLOAD
VERIFY_DATA
WRITE_COMMIT
VERIFY_COMMIT
COMPLETE
ERROR_RETRY
ERROR
```

### Invariants obligatoires

- une seule opération backend active ;
- l’ancien record actif reste intact jusqu’à la validation complète du nouveau ;
- le snapshot est immuable pendant le commit ;
- le snapshot contient exactement `cachedDataSize_u32` octets ;
- la commit unit reste vierge jusqu’à la dernière étape ;
- un succès n’est annoncé qu’après écriture, relecture et validation du commit, puis sélection en RAM du nouveau slot actif ;
- une erreur laisse l’objet `dirty` et conserve l’ancien record ;
- les retries sont bornés ;
- aucune programmation n’est effectuée si l’image est inchangée ;
- les temps proviennent exclusivement de `FMKCPU_GetTick()`.

### Générations

Par objet, gérer au minimum :

- `currentGeneration_u32` ;
- `committedGeneration_u32` ;
- `cachedDataSize_u32` ;
- `dirty_b` ;
- `hasValidRecord_b`.

Lorsqu’une requête devient active :

1. copier la taille courante et le cache dans le snapshot partagé ;
2. mémoriser `snapshotGeneration_u32` et `snapshotDataSize_u32` ;
3. ne plus modifier ce snapshot jusqu’à la fin.

Un `FMKNVM_Set()` pendant un commit :

- modifie uniquement le cache ;
- incrémente la génération courante ;
- ne modifie pas le snapshot ;
- laisse l’objet dirty pour un commit ultérieur.

À la réussite :

- `committedGeneration_u32 = snapshotGeneration_u32` ;
- `dirty_b = FALSE` uniquement si la génération courante est égale à la génération commitée.

### Collecte et partitions

- chaque objet possède une partition fixe indépendante ;
- aucune collecte globale multi-objets ;
- la collecte d’un objet ne lit, ne déplace et n’efface jamais les records d’un autre objet ;
- si l’effacement est requis, la partition contient au moins deux erase units ;
- une erase unit contenant l’unique record valide ne peut pas être effacée ;
- le nouveau record est validé dans une autre erase unit avant l’effacement de l’ancienne.

---

## 11. Phase F — FIFO, commits et politique d’écriture

Implémente :

- `FMKNVM_RequestCommit()` ;
- fusion des requêtes ;
- callbacks statiques ;
- `FMKNVM_CommitSync()` ;
- `FMKNVM_CommitAllSync()` ;
- commits automatiques ;
- quiet period ;
- délai différé maximal ;
- intervalle minimal entre deux commits ;
- FIFO exclusivement via `Library/FIFO`.

Une requête mémorise seulement :

- ObjectId ;
- génération minimale demandée ;
- callbacks et contextes ;
- aucune copie du payload.

Une requête existante peut satisfaire une demande plus ancienne si elle garantit une génération au moins égale.

`CommitAllSync()` photographie les générations dirty à son entrée puis les traite séquentiellement. Il n’est pas atomique entre objets.

N’implémente pas de restauration de valeurs par défaut.

### Contrat exact de `FMKNVM_Cyclic()`

À la fin de chaque appel :

| Situation | Retour générique | Statut NVM |
|---|---|---|
| module valide, aucune requête et aucune transaction | `RC_OK` | `FMKNVM_STATUS_OK` |
| transaction active ou requête restante | `RC_OK` | `FMKNVM_STATUS_PENDING` |
| transaction terminée avec succès | `RC_OK` | `FMKNVM_STATUS_OK` |
| erreur finalisée | code générique correspondant | statut NVM détaillé |
| module/paramètre/état invalide | code générique correspondant | statut NVM détaillé |

L’absence de travail n’est jamais une erreur et ne produit pas de code `NO_OPERATION`.

---

## 12. Phase G — Ordonnancement et appels synchrones coopératifs

Implémente :

```c
const t_sFMKNVM_BackendApi * FMKNVM_Specific_GetBackendApi(void);
void FMKNVM_Specific_ServiceDependencies(void);
```

### Contexte propriétaire unique

Toutes les API publiques et `FMKNVM_Cyclic()` sont appelées depuis le même contexte propriétaire.

Ne crée pas de mutex interne. Ne documente pas un contrat fondé sur un mutex externe arbitraire.

Un callback peut appeler :

- `FMKNVM_Set()` ;
- `FMKNVM_RequestCommit()`.

Un callback ne peut pas appeler :

- `FMKNVM_CommitSync()` ;
- `FMKNVM_CommitAllSync()`.

### Mode non bloquant

Le contexte propriétaire appelle dans l’ordre :

```text
1. FMKI2C_Cyclic() et/ou FMKSPI_Cyclic() selon la variante
2. autres dépendances explicites
3. FMKNVM_Cyclic()
```

`FMKNVM_Cyclic()` peut appeler `backend->Cyclic()`, mais n’appelle pas directement `FMKI2C_Cyclic()` ou `FMKSPI_Cyclic()` en fonctionnement normal.

### Mode synchrone avec backend asynchrone

`FMKNVM_CommitSync()` et `FMKNVM_CommitAllSync()` utilisent la même machine d’état que le mode cyclique.

À chaque itération de la boucle coopérative :

```text
1. FMKNVM_Specific_ServiceDependencies()
2. s_FMKNVM_ProgressOnce()
3. vérification de la génération demandée, du résultat et du timeout
```

Il est interdit de simplement boucler sur `IsBusy()`.

Pour un backend I²C IT/DMA, les tests doivent démontrer que `CommitSync()` progresse et termine même lorsque le scheduler externe est bloqué dans cet appel.

Les callbacks terminés pendant la boucle synchrone sont différés et dispatchés seulement après la sortie de la boucle interne.

### Timeout

Le timeout limite uniquement l’attente du demandeur. Il n’annule pas automatiquement l’opération physique.

Après timeout :

- retourner le code timeout et `FMKNVM_STATUS_TIMEOUT` ;
- conserver snapshot, taille et métadonnées ;
- conserver la propriété du backend ;
- ne lancer aucune autre opération physique ;
- terminer la transaction via les appels cycliques ultérieurs ;
- conserver le résultat final dans les diagnostics et callbacks attachés.

N’implémente pas d’annulation physique dans cette version.

---

## 13. Phase H — Validation de configuration

Dans `FMKNVM_Init()`, valider avant toute écriture :

### Pour chaque objet

- ObjectId unique ;
- cache non nul ;
- capacité de slot non nulle ;
- cache statique de capacité suffisante ;
- capacité inférieure ou égale aux maxima générés ;
- partition dans les limites du support ;
- aucune intersection avec une autre partition ;
- nombre de slots suffisant ;
- `partitionSize >= slotStride * slotCount`.

### Pour le layout physique

- alignement du début de slot ;
- alignement du header ;
- alignement de la zone payload ;
- alignement et offset fixe de la commit unit ;
- `maxTransferSize_u32 > 0` ;
- capacité du snapshot et du buffer de lecture ;
- alignement de partition sur l’erase unit si nécessaire ;
- au moins deux erase units indépendantes si `eraseRequired_b == TRUE` ;
- capacité et indépendance de la partition de maintenance.

Une configuration incohérente doit :

- retourner l’erreur de configuration générique du projet ;
- produire `FMKNVM_STATUS_CONFIGURATION_ERROR` ;
- interdire toute écriture.

---

## 14. Phase I — Maintenance persistante et quarantaine

Implémente cette phase uniquement si elle est incluse dans le périmètre de la version demandée, mais conserve les points d’extension prévus par l’architecture.

Le record de maintenance :

- possède son propre format ;
- possède sa propre taille ;
- possède sa propre capacité de slot ;
- utilise une partition dédiée ;
- utilise le même protocole header/CRC/commit ;
- possède un identifiant interne réservé.

Un slot applicatif est mis en quarantaine uniquement après le nombre configuré d’échecs consécutifs de programmation ou de vérification locale. Une erreur de bus transitoire seule ne suffit pas.

Les slots de maintenance ne se mettent jamais eux-mêmes en quarantaine persistante.

Si la maintenance persistante échoue :

- conserver la quarantaine courante en RAM jusqu’au reset ;
- ne pas relancer une écriture en boucle ;
- remonter un diagnostic critique ;
- revalider tous les slots applicatifs au démarrage suivant ;
- ne supposer aucune quarantaine persistante valide tant qu’un nouveau record de maintenance n’est pas validé.

---

## 15. Phase J — Backends matériels

N’implémente les backends matériels qu’après réussite complète du backend RAM et des tests de coupure.

### Backend Flash STM32G4

- page G4 privée au backend = erase unit ;
- double-word = program unit ;
- commit dans une program unit séparée ;
- alignement, ECC et flags selon le MCU réel ;
- routines en RAM si nécessaire ;
- zone NVM réservée par le linker ;
- aucune reprogrammation d’une program unit non vierge.

### Backend Flash STM32H7

- secteur H7 privé au backend = erase unit ;
- flash word = program unit ;
- commit dans un flash word séparé ;
- gestion des banques, ECC, caches et flags selon la sous-famille réelle ;
- aucune hypothèse de taille de secteur dans le cœur.

### Backend EEPROM I²C

- utiliser exclusivement `FMK_I2C` ;
- configuration privée : bus, adresse, capacité, taille d’adresse mémoire, taille d’EEPROM page, timeout ;
- découper toute programmation aux frontières d’EEPROM page ;
- ACK polling ou mécanisme réel du projet ;
- relecture de vérification ;
- comportement IT/DMA compatible avec `FMKNVM_Specific_ServiceDependencies()` ;
- ne jamais interpréter une EEPROM page comme un slot ou une erase unit.

### Backend EEPROM SPI

- utiliser exclusivement `FMK_SPI` ;
- configuration privée : bus, chip-select, opcodes, capacité, taille d’EEPROM page, timeout ;
- WREN ;
- WIP polling ;
- découpage aux frontières d’EEPROM page ;
- relecture de vérification ;
- comportement asynchrone compatible avec le service coopératif.

---

## 16. Tests d’acceptation obligatoires

Les tests doivent couvrir au minimum :

1. objets de capacités et tailles logiques différentes ;
2. tailles 7, 32, 96, 255 et 512 octets ;
3. deux écritures successives du même objet avec des tailles différentes ;
4. `Set()` taille zéro, valide et supérieure à la capacité ;
5. `Get()` sans record, buffer trop petit, exact et supérieur ;
6. aucune copie partielle ;
7. déduplication par comparaison taille + contenu ;
8. FIFO sans payload ;
9. fusion des requêtes et callbacks ;
10. snapshot immuable pendant `WRITE_PAYLOAD` ;
11. évolution du cache avant activation d’une requête ;
12. coupure après chaque état ;
13. coupure après chaque octet programmé ;
14. corruption séparée du header, payload logique et commit ;
15. modification de la zone payload réservée hors payload logique ;
16. rejet de `payloadSize == 0` ;
17. rejet de `payloadSize > slotPayloadCapacity_u32` ;
18. rotation des slots ;
19. collecte entre erase units ;
20. absence d’accès à une autre partition pendant une collecte ;
21. overflow de séquence ;
22. retry et maintien de `dirty` ;
23. erreur de configuration : cache nul, capacité nulle, chevauchement, alignement, partition insuffisante, maxima sous-dimensionnés ;
24. `FMKNVM_Cyclic()` idle = `RC_OK + STATUS_OK` ;
25. `FMKNVM_Cyclic()` pending = `RC_OK + STATUS_PENDING` ;
26. `CommitSync()` attaché à une requête existante ;
27. timeout avec backend actif ;
28. reprise cyclique après timeout ;
29. backend I²C/SPI asynchrone progressant via `FMKNVM_Specific_ServiceDependencies()` ;
30. preuve qu’une boucle sans service des dépendances finirait en timeout dans le mock ;
31. callbacks différés hors boucle synchrone ;
32. absence d’appel `FMKNVM_*` depuis ISR ;
33. contexte propriétaire unique ;
34. segmentation distincte : slots, erase units et EEPROM pages ;
35. budget RAM réel documenté ;
36. quarantaine et record de maintenance si cette phase est implémentée.

Les mêmes tests fonctionnels du cœur doivent être exécutés avec le backend RAM puis, lorsque disponibles, avec les backends G4, H7, EEPROM I²C et EEPROM SPI.

---

## 17. Critères de fin

L’implémentation n’est considérée terminée que si :

- elle respecte tous les `AGENTS.md` applicables ;
- elle compile avec les variantes concernées ;
- tous les tests de la phase réussissent ;
- `git diff --check` ne remonte aucune erreur ;
- aucun type fondamental ou code générique concurrent n’a été introduit ;
- aucune HAL interdite ne traverse le cœur ;
- les objets de tailles variables sont réellement supportés ;
- aucune valeur par défaut ou migration métier n’a été ajoutée au cœur ;
- les invariants de coupure sont testés ;
- les limitations et phases non implémentées sont explicitement documentées.

À la fin, fournis :

- la liste des fichiers créés et modifiés ;
- les choix de typage et leur source dans le dépôt ;
- les commandes exécutées ;
- les résultats des builds et tests ;
- le budget RAM/NVM calculé ;
- les avertissements restants ;
- les exigences non implémentées ;
- un résumé du diff limité au périmètre FMK_NVM.

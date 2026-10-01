export interface BenchmarkImageAsset {
  game: string;
  cardIds: string[];
  module: number;
}

// Images used for benchmarking the scanner pipeline.
export const BENCHMARK_IMAGES: BenchmarkImageAsset[] = [
  // --- chrono-core ---
  {
    game: 'chrono-core',
    // Ryu Visor V.1 (WNR01-082)
    cardIds: ['6a6a83e5cbe85c607d7fc276'],
    module: require('../assets/games_images/chrono-core/card_1.jpg'),
  },
  {
    game: 'chrono-core',
    // Basilisk Visor V.1 (WNR01-015)
    cardIds: ['6a6a83e3d98793368de28e4e'],
    module: require('../assets/games_images/chrono-core/card_2.jpg'),
  },
  {
    game: 'chrono-core',
    // Rawhide V.1 (WNR01-051)
    cardIds: ['6a6a83e4dabd2f626f2d675e'],
    module: require('../assets/games_images/chrono-core/card_3.jpg'),
  },
  {
    game: 'chrono-core',
    // Ryu Core V.1 (WNR01-008)
    cardIds: ['6a6a83e3a2531ee0b59acf21'],
    module: require('../assets/games_images/chrono-core/card_4.jpg'),
  },
  {
    game: 'chrono-core',
    // Duskwulf Greaves V.1 (WNR01-056)
    cardIds: ['6a6a83e4cf5e9a34b2603b66'],
    module: require('../assets/games_images/chrono-core/card_5.jpg'),
  },
  // --- cyberpunk ---
  {
    game: 'cyberpunk',
    // Saburo Arasaka - Stubborn Patriach (α029)
    cardIds: ['69675c6c8b5e9ad6eb87a342', '69675c6c8b5e9ad6eb87a340'],
    module: require('../assets/games_images/cyberpunk/card_1.jpg'),
  },
  {
    game: 'cyberpunk',
    // V - Streetkid (Female) (132)
    cardIds: ['69d0b5667ae64521f41aa5f1'],
    module: require('../assets/games_images/cyberpunk/card_2.jpg'),
  },
  // --- dbs-fusion ---
  {
    game: 'dbs-fusion',
    // Saibaiman (FB04-056)
    cardIds: ['6a58e61e2eca35a18e0aed49'],
    module: require('../assets/games_images/dbs-fusion/card_1.jpg'),
  },
  {
    game: 'dbs-fusion',
    // Vegeta - FB04-089 (FB04-089)
    cardIds: ['6a58e61ff90149a6e1a4e2aa'],
    module: require('../assets/games_images/dbs-fusion/card_2.jpg'),
  },
  {
    game: 'dbs-fusion',
    // Overwhelming Confidence (FB04-047)
    cardIds: ['6a58e61e91a0ba7efa8190c1'],
    module: require('../assets/games_images/dbs-fusion/card_3.jpg'),
  },
  {
    game: 'dbs-fusion',
    // Whis (FB04-002)
    cardIds: ['6a58e61f43fbe85fff966f06', '6a58e621e09c334b18f0ae54'],
    module: require('../assets/games_images/dbs-fusion/card_4.jpg'),
  },
  {
    game: 'dbs-fusion',
    // Hercule (FB04-097)
    cardIds: ['6a58e61d15b570c44a7ef531', '6a58e6203dcebe780d3c854e'],
    module: require('../assets/games_images/dbs-fusion/card_5.jpg'),
  },
  // --- dbs-masters ---
  {
    game: 'dbs-masters',
    // Son Goku & Vegeta, Battling Against Evil (SPR) (BT31-010)
    cardIds: ['6a58e8ebb426b3500723806e'],
    module: require('../assets/games_images/dbs-masters/card_1.jpg'),
  },
  {
    game: 'dbs-masters',
    // Majin Buu, Fear Incarnate (SPR) (BT31-137)
    cardIds: ['6a58e8ea0a67b951938d4eb6'],
    module: require('../assets/games_images/dbs-masters/card_2.jpg'),
  },
  {
    game: 'dbs-masters',
    // SS4 Son Goku, Vow of Victory (SPR) (BT31-044)
    cardIds: ['6a58e8eb6a6c07b977972b05'],
    module: require('../assets/games_images/dbs-masters/card_3.jpg'),
  },
  {
    game: 'dbs-masters',
    // SS4 Gogeta, the Light Protecting the Universe (SPR) (BT31-052)
    cardIds: ['6a58e8eb4065b8210d5dec46'],
    module: require('../assets/games_images/dbs-masters/card_4.jpg'),
  },
  {
    game: 'dbs-masters',
    // Beerus, God of Destruction's Pride (SPR) (BT31-033)
    cardIds: ['6a58e8e938ea5ed18acb39b9'],
    module: require('../assets/games_images/dbs-masters/card_5.jpg'),
  },
  // --- eoa ---
  {
    game: 'eoa',
    // Varenthus, the Skyscourge (IMP111)
    cardIds: ['6a362e4de87ce69bf6886c7a', '6a362e4e8fdbc8a3d354d98e'],
    module: require('../assets/games_images/eoa/card_1.jpg'),
  },
  {
    game: 'eoa',
    // Genesis III, Deus Volare (RO001)
    cardIds: ['6a362e4ed69cb30320c9f5e2', '6a362e4c3e51bcce8e0845f7'],
    module: require('../assets/games_images/eoa/card_2.jpg'),
  },
  {
    game: 'eoa',
    // Hongzhen, the Scarlet Oracle (IMP102)
    cardIds: ['6a362e4d4d9d12fb66119ee2'],
    module: require('../assets/games_images/eoa/card_3.jpg'),
  },
  {
    game: 'eoa',
    // Biscuit, Ever Vigilant (IMP038)
    cardIds: ['6a362e4ca66a8a6c35d4c337'],
    module: require('../assets/games_images/eoa/card_4.jpg'),
  },
  {
    game: 'eoa',
    // Lingyi, the Gilded Prophet (IMP105)
    cardIds: ['6a362e4dba271d9c4149387d', '6a362e4e19dd012339a9891b'],
    module: require('../assets/games_images/eoa/card_5.jpg'),
  },
  // --- fab ---
  {
    game: 'fab',
    // Heroic Grit (Yellow) (SUP056)
    cardIds: ['68d1103bd292211af992eb82'],
    module: require('../assets/games_images/fab/card_1.jpg'),
  },
  {
    game: 'fab',
    // Chest Puff (Red) (SUP222)
    cardIds: ['68d1103bd292211af992ea77'],
    module: require('../assets/games_images/fab/card_2.jpg'),
  },
  {
    game: 'fab',
    // Buckwild (Blue) (SUP145)
    cardIds: [
      '68d1103bd292211af992e9da',
      '68d1103bd292211af992e9d6',
      '68d1103bd292211af992e9d8',
    ],
    module: require('../assets/games_images/fab/card_3.jpg'),
  },
  {
    game: 'fab',
    // Sit! (Red) (SUP210)
    cardIds: ['68d1103bd292211af992ea68'],
    module: require('../assets/games_images/fab/card_4.jpg'),
  },
  {
    game: 'fab',
    // What Happens Next? (Blue) (SUP209)
    cardIds: ['68d1103bd292211af992ea66', '68d1103bd292211af992eaf7'],
    module: require('../assets/games_images/fab/card_5.jpg'),
  },
  // --- grand-archive ---
  {
    game: 'grand-archive',
    // Idle Thoughts (049)
    cardIds: [
      '6a3631734b0a9ed48461343a',
      '6a3631caa07942d0bd228425',
      '6a3631caa415d7739ddce32e',
    ],
    module: require('../assets/games_images/grand-archive/card_1.jpg'),
  },
  {
    game: 'grand-archive',
    // Swerving Spring (085)
    cardIds: ['6a3631751dc752aa45ecf1ba'],
    module: require('../assets/games_images/grand-archive/card_2.jpg'),
  },
  {
    game: 'grand-archive',
    // Infusion of Crescent Jade (180)
    cardIds: ['6a36317321dbd1c36f5a4896'],
    module: require('../assets/games_images/grand-archive/card_3.jpg'),
  },
  {
    game: 'grand-archive',
    // Primal Whip (020)
    cardIds: ['6a3631742356e4aa3c9e4ec1'],
    module: require('../assets/games_images/grand-archive/card_4.jpg'),
  },
  {
    game: 'grand-archive',
    // Adorned Stag (030)
    cardIds: ['6a363171317e8cf1b07a7f30'],
    module: require('../assets/games_images/grand-archive/card_5.jpg'),
  },
  // --- gundam ---
  {
    game: 'gundam',
    // Privileged Position (GD03-102)
    cardIds: ['6a58854455195ea40d69d30a'],
    module: require('../assets/games_images/gundam/card_1.jpg'),
  },
  {
    game: 'gundam',
    // Duel Gundam (Assault Shroud) (GD03-042) (GD03-042)
    cardIds: ['6a5885439dcc134c05da1568'],
    module: require('../assets/games_images/gundam/card_2.jpg'),
  },
  {
    game: 'gundam',
    // Over the River and Through the Woods (GD03-107)
    cardIds: ['6a5885456ce4f32108f95af3'],
    module: require('../assets/games_images/gundam/card_3.jpg'),
  },
  {
    game: 'gundam',
    // Hotarubi (GD03-129)
    cardIds: ['6a588542a8052b58d525c3e8', '6a5885375e597de36f68ff99'],
    module: require('../assets/games_images/gundam/card_4.jpg'),
  },
  {
    game: 'gundam',
    // Hy-Gogg (T-013) (T-013)
    cardIds: ['6a58854576888565fa862bef'],
    module: require('../assets/games_images/gundam/card_5.jpg'),
  },
  // --- lorcana ---
  {
    game: 'lorcana',
    // Aladdin - Intrepid Commander (119)
    cardIds: ['684f3f85373bb58ecee0189a'],
    module: require('../assets/games_images/lorcana/card_1.jpg'),
  },
  {
    game: 'lorcana',
    // Basil - Hypnotized Mouse (79)
    cardIds: ['684f3f84373bb58ecee01739'],
    module: require('../assets/games_images/lorcana/card_2.jpg'),
  },
  {
    game: 'lorcana',
    // Yokai - Professor Callaghan (158)
    cardIds: ['684f3f85373bb58ecee01998'],
    module: require('../assets/games_images/lorcana/card_3.jpg'),
  },
  {
    game: 'lorcana',
    // Aladdin - Fearless Navigator (112)
    cardIds: ['684f3f85373bb58ecee01868'],
    module: require('../assets/games_images/lorcana/card_4.jpg'),
  },
  {
    game: 'lorcana',
    // Billy Bones - Space Sailor (185)
    cardIds: ['684f3f85373bb58ecee01a2a'],
    module: require('../assets/games_images/lorcana/card_5.jpg'),
  },
  // --- mtg ---
  {
    game: 'mtg',
    // Amazing Acrobatics (25)
    cardIds: ['68ee1d28b37fd6bebc32618c'],
    module: require('../assets/games_images/mtg/card_1.jpg'),
  },
  {
    game: 'mtg',
    // Maximum Carnage (83)
    cardIds: ['68ee1d28b37fd6bebc3260e3', '68ee1d28b37fd6bebc3260e2'],
    module: require('../assets/games_images/mtg/card_2.jpg'),
  },
  {
    game: 'mtg',
    // Pumpkin Bombardment (139)
    cardIds: ['68ee1d29b37fd6bebc3261cd'],
    module: require('../assets/games_images/mtg/card_3.jpg'),
  },
  {
    game: 'mtg',
    // Venom's Hunger (73)
    cardIds: ['688e481e7d3de933adc87fc8'],
    module: require('../assets/games_images/mtg/card_4.jpg'),
  },
  {
    game: 'mtg',
    // Sun-Spider, Nimble Webber (154)
    cardIds: ['68ee1d29b37fd6bebc3261dc'],
    module: require('../assets/games_images/mtg/card_5.jpg'),
  },
  // --- naruto-mythos ---
  {
    game: 'naruto-mythos',
    // Zaku Abumi - Overconfident Shinobi (070)
    cardIds: [
      '6a47cb407cb39b7c325a019e',
      '69d4ec5293c168406d74149c',
      '69d4ec5293c168406d74149d',
      '6a47cb40bdd8978f0df786ec',
    ],
    module: require('../assets/games_images/naruto-mythos/card_1.jpg'),
  },
  {
    game: 'naruto-mythos',
    // Naruto Uzumaki - Genin of the Leaf Village (009)
    cardIds: [
      '6a47cb3e4115820031c3a49b',
      '69d4ec5293c168406d7414bf',
      '69d4ec5293c168406d7414cc',
      '6a47cb3ef99a0a3f9424d6b6',
    ],
    module: require('../assets/games_images/naruto-mythos/card_2.jpg'),
  },
  {
    game: 'naruto-mythos',
    // Jirobo - Bearer of the Curse Mark (057)
    cardIds: [
      '6a47cb404316e2b51c09bff1',
      '69d4ec5293c168406d741400',
      '69d4ec5293c168406d74140b',
      '6a47cb40763585e5263536a1',
    ],
    module: require('../assets/games_images/naruto-mythos/card_3.jpg'),
  },
  {
    game: 'naruto-mythos',
    // Kin Tsuchi - Bell Sound Clone (073)
    cardIds: [
      '6a47cb4012d3a24c85885cef',
      '69d4ec5293c168406d7414a3',
      '69d4ec5293c168406d7414a4',
      '6a47cb40f8f2150cb53518a5',
    ],
    module: require('../assets/games_images/naruto-mythos/card_4.jpg'),
  },
  {
    game: 'naruto-mythos',
    // Baki - Council Agent (081)
    cardIds: [
      '6a47cb4133420e448350f161',
      '69d4ec5293c168406d7414b7',
      '69d4ec5293c168406d7414b8',
      '6a47cb41e042b935c24afece',
    ],
    module: require('../assets/games_images/naruto-mythos/card_5.jpg'),
  },
  // --- onepiece ---
  {
    game: 'onepiece',
    // To Never Doubt--That Is Power! (OP12-016)
    cardIds: ['684f468c373bb58ecee0212c', '69937a797b765fa76d6ddefb'],
    module: require('../assets/games_images/onepiece/card_1.jpg'),
  },
  {
    game: 'onepiece',
    // Monet (OP12-076)
    cardIds: ['684f468c373bb58ecee0226c', '69937a797b765fa76d6ddf6a'],
    module: require('../assets/games_images/onepiece/card_2.jpg'),
  },
  {
    game: 'onepiece',
    // Donquixote Rosinante (048) (OP12-048)
    cardIds: ['684f468c373bb58ecee021d8', '69937a797b765fa76d6ddfaf'],
    module: require('../assets/games_images/onepiece/card_3.jpg'),
  },
  {
    game: 'onepiece',
    // Belo Betty (OP12-090)
    cardIds: ['684f468c373bb58ecee0229c', '69937a7a7b765fa76d6de0d3'],
    module: require('../assets/games_images/onepiece/card_4.jpg'),
  },
  {
    game: 'onepiece',
    // Lindbergh (OP12-095)
    cardIds: ['684f468c373bb58ecee022be', '69937a7a7b765fa76d6de0fc'],
    module: require('../assets/games_images/onepiece/card_5.jpg'),
  },
  // --- palworld ---
  {
    game: 'palworld',
    // Tombat – Out of Nowhere!? (EBP01-076)
    cardIds: ['6a6a7d638f7a9d657ae59cce', '6a6a7d632c7ebd896faab1b2'],
    module: require('../assets/games_images/palworld/card_1.jpg'),
  },
  {
    game: 'palworld',
    // Warsect – Iron Fortress (EBP01-054SR)
    cardIds: ['6a6a7d62a875e8505d667a76', '6a6a7d62824098e3afdad2ff'],
    module: require('../assets/games_images/palworld/card_2.jpg'),
  },
  {
    game: 'palworld',
    // Katress – Abyssal Sorcerer (EBP01-075SP)
    cardIds: ['6a6a7d633fd38a63e89d03b1'],
    module: require('../assets/games_images/palworld/card_3.jpg'),
  },
  {
    game: 'palworld',
    // Relaxaurus – Hungry Gunner (EBP01-026)
    cardIds: ['6a6a7d611bc4633a29978f55', '6a6a7d6191a309cb2b991d17'],
    module: require('../assets/games_images/palworld/card_4.jpg'),
  },
  {
    game: 'palworld',
    // Wumpo Botan – Tropical Sentinel (EBP01-062)
    cardIds: ['6a6a7d63b216008aaa69e99f'],
    module: require('../assets/games_images/palworld/card_5.jpg'),
  },
  // --- pokemon ---
  {
    game: 'pokemon',
    // Helioptile (052)
    cardIds: ['69492ab485510f6d5f276985'],
    module: require('../assets/games_images/pokemon/card_1.jpg'),
  },
  {
    game: 'pokemon',
    // Crawdaunt (085)
    cardIds: ['69492ab485510f6d5f276bc4'],
    module: require('../assets/games_images/pokemon/card_2.jpg'),
  },
  {
    game: 'pokemon',
    // Makuhita (072)
    cardIds: ['69492ab485510f6d5f276afb'],
    module: require('../assets/games_images/pokemon/card_3.jpg'),
  },
  {
    game: 'pokemon',
    // Pyroar (024)
    cardIds: ['69492ab385510f6d5f276800'],
    module: require('../assets/games_images/pokemon/card_4.jpg'),
  },
  {
    game: 'pokemon',
    // Cinderace (028)
    cardIds: ['69492ab385510f6d5f276838'],
    module: require('../assets/games_images/pokemon/card_5.jpg'),
  },
  // --- pokemon-japan ---
  {
    game: 'pokemon-japan',
    // Ciphermaniacs Codebreaking (067)
    cardIds: [
      '69d4f44e93c168406d755b6f',
      '69d4f45b93c168406d7586bc',
      '69d4f43993c168406d7518bb',
      '69d4f43993c168406d7518bc',
    ],
    module: require('../assets/games_images/pokemon-japan/card_1.jpg'),
  },
  {
    game: 'pokemon-japan',
    // Torracat (021)
    cardIds: ['69d4f44e93c168406d755b67'],
    module: require('../assets/games_images/pokemon-japan/card_2.jpg'),
  },
  {
    game: 'pokemon-japan',
    // Meditite (037)
    cardIds: ['69d4f44e93c168406d755b99'],
    module: require('../assets/games_images/pokemon-japan/card_3.jpg'),
  },
  {
    game: 'pokemon-japan',
    // Dark Dugtrio (dark-dugtrio)
    cardIds: ['69d4f44593c168406d7540c1'],
    module: require('../assets/games_images/pokemon-japan/card_4.jpg'),
  },
  {
    game: 'pokemon-japan',
    // Mega Greninja ex (114)
    cardIds: ['69d4f43693c168406d75157d'],
    module: require('../assets/games_images/pokemon-japan/card_5.jpg'),
  },
  // --- riftbound ---
  {
    game: 'riftbound',
    // Teemo - Scout (197b)
    cardIds: ['6906977c435a8703d971169f'],
    module: require('../assets/games_images/riftbound/card_1.jpg'),
  },
  {
    game: 'riftbound',
    // Viktor - Innovator (176)
    cardIds: ['6a5bb31ce1f8bae26f4aa4fd'],
    module: require('../assets/games_images/riftbound/card_2.jpg'),
  },
  {
    game: 'riftbound',
    // LeBlanc - Deceiver (Signature) (235*)
    cardIds: ['69d026fd7ae64521f4144341'],
    module: require('../assets/games_images/riftbound/card_3.jpg'),
  },
  {
    game: 'riftbound',
    // Ivern - Green Father (Signature) (233*)
    cardIds: ['69d026fd7ae64521f414432a', '69d026fd7ae64521f4144329'],
    module: require('../assets/games_images/riftbound/card_4.jpg'),
  },
  {
    game: 'riftbound',
    // Pyke - Bloodharbor Ripper (Signature) (228*)
    cardIds: ['69d026fd7ae64521f4144377'],
    module: require('../assets/games_images/riftbound/card_5.jpg'),
  },
  // --- rise ---
  {
    game: 'rise',
    // The Macaron (219)
    cardIds: ['6900e440435a8703d95a727c', '6900e440435a8703d95a73c3'],
    module: require('../assets/games_images/rise/card_1.jpg'),
  },
  {
    game: 'rise',
    // Training Result (undefined)
    cardIds: ['6900e440435a8703d95a77e5'],
    module: require('../assets/games_images/rise/card_2.jpg'),
  },
  {
    game: 'rise',
    // Sorcerer'S Mask (204)
    cardIds: ['6900e440435a8703d95a724f', '6900e440435a8703d95a73ba'],
    module: require('../assets/games_images/rise/card_3.jpg'),
  },
  {
    game: 'rise',
    // Isaac (016)
    cardIds: ['6900f6b2435a8703d95a84e9'],
    module: require('../assets/games_images/rise/card_4.jpg'),
  },
  {
    game: 'rise',
    // Wrath (215)
    cardIds: [
      '6900e441435a8703d95a7f6b',
      '6900e441435a8703d95a8152',
      '6a32398942bbe0993c13a536',
    ],
    module: require('../assets/games_images/rise/card_5.jpg'),
  },
  // --- sorcery ---
  {
    game: 'sorcery',
    // Fade (undefined)
    cardIds: ['68500ddd373bb58ecee02b03', '68500ddd373bb58ecee02afe'],
    module: require('../assets/games_images/sorcery/card_1.jpg'),
  },
  {
    game: 'sorcery',
    // House Arn Bannerman (undefined)
    cardIds: ['68500ddd373bb58ecee02d14', '68500ddd373bb58ecee02d08'],
    module: require('../assets/games_images/sorcery/card_2.jpg'),
  },
  {
    game: 'sorcery',
    // Bury (undefined)
    cardIds: ['68500ddd373bb58ecee02e37', '68500ddd373bb58ecee02e34'],
    module: require('../assets/games_images/sorcery/card_3.jpg'),
  },
  {
    game: 'sorcery',
    // Porcupine Pufferfish (undefined)
    cardIds: ['68500ddd373bb58ecee02b11', '68500ddd373bb58ecee02b30'],
    module: require('../assets/games_images/sorcery/card_4.jpg'),
  },
  {
    game: 'sorcery',
    // Belmotte Longbowmen (undefined)
    cardIds: ['68500ddd373bb58ecee02c79', '68500ddd373bb58ecee02c72'],
    module: require('../assets/games_images/sorcery/card_5.jpg'),
  },
  // --- swu ---
  {
    game: 'swu',
    // Corellian Freighter (258/262)
    cardIds: [
      '69d4eb8d93c168406d73f0d5',
      '69d4eb8d93c168406d73f0d7',
      '69d4eb8f93c168406d73f72c',
    ],
    module: require('../assets/games_images/swu/card_1.jpg'),
  },
  {
    game: 'swu',
    // Vonreg's TIE Interceptor - Ace of the First Order (137/262)
    cardIds: [
      '69d4eb8d93c168406d73efff',
      '69d4eb8d93c168406d73f000',
      '69d4eb8d93c168406d73f001',
      '69d4eb8d93c168406d73f002',
    ],
    module: require('../assets/games_images/swu/card_2.jpg'),
  },
  {
    game: 'swu',
    // Silver Angel - Trace's Hope (062/262)
    cardIds: [
      '69d4eb8d93c168406d73ef55',
      '69d4eb8d93c168406d73ef56',
      '69d4eb8d93c168406d73ef57',
      '69d4eb8d93c168406d73ef58',
    ],
    module: require('../assets/games_images/swu/card_3.jpg'),
  },
  {
    game: 'swu',
    // Dorsal Turret (120/262)
    cardIds: [
      '69d4eb8d93c168406d73ef18',
      '69d4eb8d93c168406d73ef19',
      '69d4eb8d93c168406d73ef1a',
      '69d4eb8d93c168406d73ef1b',
    ],
    module: require('../assets/games_images/swu/card_4.jpg'),
  },
  {
    game: 'swu',
    // Focus Fire (129/262)
    cardIds: [
      '69d4eb8d93c168406d73eff3',
      '69d4eb8d93c168406d73eff4',
      '69d4eb8d93c168406d73eff5',
      '69d4eb8d93c168406d73eff6',
    ],
    module: require('../assets/games_images/swu/card_5.jpg'),
  },
];

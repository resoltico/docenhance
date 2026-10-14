# Public document corpus

The [source corpus](https://github.com/resoltico/docenhance/tree/main/corpus) contains historical
document images for technical processing and preservation checks. `corpus/manifest.json` owns the
item inventory, provenance, retrieval timestamps, byte identities and rights evidence. Images and
their accompanying records are separate from the project's MPL-2.0 code and documentation.

## Admission and redistribution

Admit an image only when an authoritative, item-specific public-domain determination or explicit
[CC0 dedication](https://creativecommons.org/publicdomain/zero/1.0/legalcode) supports worldwide redistribution of that image in a public Git repository and
source archives. Check both the underlying work and the supplied digital image. A collection label,
uploader's assertion, age, government custodian or CC0 metadata designation alone is insufficient.
Inspect embedded components, including ICC profiles, for their own copyright notices and terms;
an image's CC0 designation does not relicense them. Retain a component only under documented terms
permitting its inclusion and public redistribution. Exclude unclear or restrictive component terms,
including indemnification or software-bundling conditions.
Exclude unknown, contradictory, all-rights-reserved, noncommercial and no-derivatives terms.
“No known copyright restrictions” alone does not meet this policy; [Flickr explains why it is an
assessment without a public-domain warranty](https://www.flickr.org/programs/flickr-commons/no-known-copyright-restrictions-how-it-works/).
Do not retain rejected images, their derivatives or copied rights-page HTML in the project corpus.

Review primary image rights before a bounded download. Inspect the bytes and embedded components
before admitting an input to the public corpus; purge rejected copies. For every accepted item,
retain:

- The institution, source item link, creator, work date, catalogue identifier and physical document
  identity. Record unknown facts explicitly; do not invent authorship or provenance.
- The exact download and resolved URLs, rendition description, UTC retrieval timestamp, stored
  path, SHA-256, byte count and decoded dimensions. Preserve the served bytes without re-encoding.
- Image-specific rights evidence, its source URL and retained record path/hash, the applicable
  policy and legal-code links, and the reviewer's identity, assessment and scope.
- Separate licenses for the image and any retained metadata; preserve attribution and notices.
  Copy records only under their own permitted terms, rather than copying whole website pages.
- Embedded-component identities, copyright ownership, license evidence and any reuse obligations.
  Preserve the original bytes and embedded identification/copyright tags unchanged.

CC0 concerns copyright and related rights, with its legal fallback where waiver is unavailable.
It does not establish clearance of third-party privacy, publicity, trademark or patent rights.
Exclude material with unresolved applicable restrictions. Source institutions do not endorse
DocEnhance. Automated rights QA is recorded as an automated assessment, not a human rights review
or legal warranty; hashes establish byte identity, not rights.

## Included material and licenses

The manifest identifies nine historical inputs and their selected leaf or media identities.

| Input | Material and selected view | Institutional item |
| --- | --- | --- |
| `mills-manuscript` | Robert Mills's handwritten autobiographical sketch, c. 1836–40 | [Smithsonian National Portrait Gallery](https://npg.si.edu/object/npg_NPG.POB133) |
| `express-receipt` | Filled printed package receipt, 12 February 1872; handwriting and signature | [Smithsonian National Postal Museum](https://postalmuseum.si.edu/object/npm_2014.2027.14) |
| `armenian-incipit` | Malnazar and Aghap'ir's decorated Armenian incipit, 1637–38; leaf 488 verso | [J. Paul Getty Museum](https://www.getty.edu/art/collection/object/107SQC) |
| `carolingian-text` | Carolingian manuscript text, early 9th century; leaf 4 verso | [J. Paul Getty Museum](https://www.getty.edu/art/collection/object/108FET) |
| `astronomical-volvelle` | Astronomical table with movable pointers, shortly after 1386; leaf 51 | [J. Paul Getty Museum](https://www.getty.edu/art/collection/object/108GFE) |
| `antiphonal-notation` | Francesco di Antonio del Chierico's antiphonal, early 1460s; selected canvas for leaf 2 verso, with notation and red staves | [J. Paul Getty Museum](https://www.getty.edu/art/collection/object/1095S6) |
| `calendar-columns` | Calendar with aligned entries and marginal additions, after 1234 and before 1262; leaf 5 | [J. Paul Getty Museum](https://www.getty.edu/art/collection/object/107SYR) |
| `thames-distance-table` | Henry W. Taunt's Thames guide, 1897; Table foldout with triangular distance matrix and advertisement blocks | [J. Paul Getty Museum](https://www.getty.edu/art/collection/object/104HNE) |
| `thames-map` | The same Taunt guide, 1897; Map foldout with fine lines, small labels and legend | [J. Paul Getty Museum](https://www.getty.edu/art/collection/object/104HNE) |

The Smithsonian inputs retain their original high-resolution JPEGs; Getty inputs retain
institution-provided complete-sheet renditions. The manifest records each exact rendition and
decoded dimensions; downloaded bytes are unchanged. The two Thames foldouts share one source book
and provenance record; they do not represent two independent books or source collections.

All scan imagery is explicitly CC0-1.0. Smithsonian records designate both the specific media and
metadata CC0; [its Open Access FAQ](https://www.si.edu/openaccess/faq) explains that metadata for
other, restricted objects can also be CC0. Getty's image designation applies to the images; its
collection text and metadata have separate CC-BY-4.0 terms, as explained in [Getty's collection
terms](https://www.getty.edu/legal/terms-of-use/) and [Open Content policy](https://www.getty.edu/projects/open-content-program/).

The retained Getty records are credited to the J. Paul Getty Trust, linked to their original item
and record URLs in the manifest, and preserved without modification under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).
Per-file `.license` notices identify the applicable terms; legal codes are retained in
`corpus/licenses/`. These upstream images and records are not relicensed MPL-2.0.

Each JPEG also embeds an ICC-owned color profile under a separate permissive license. The
[ICC profile library](https://registry.color.org/profile-library/) permits copying, distribution,
embedding and sale of ICC-owned profiles. `corpus/licenses/LicenseRef-ICC-Profile.md` retains the
terms; the manifest identifies each embedded profile by byte count, SHA-256, ownership and source
evidence. The complete JPEG's license expression is `CC0-1.0 AND LicenseRef-ICC-Profile`, rather than
CC0 for every component. Preserve the unmodified profiles' identification and copyright tags.
Altered profiles must remove the original identification and copyright information and must not
be presented as the original profiles, as the ICC terms require.

The corpus ships in Git and source archives. Native validation packages carry this guide, but
omit corpus assets; use the repository link above or the corresponding source archive. Refer to
the manifest for exact item links, dimensions, rendition limits and retrieval facts.

## Reviewed discovery catalogs

The supplied Flickr catalogs were reviewed on 2026-10-14. None supplied an admitted image for this
corpus. These decisions concern admission under this project's policy, not a finding that every
item in a catalog is copyrighted.

| Catalog | Decision and basis |
| --- | --- |
| [Library of Congress albums](https://www.flickr.com/photos/library_of_congress/albums/) | Discovery only. Require item-specific evidence beyond the Commons label; consult the Library's [rights guidance](https://www.loc.gov/legal/security-copyright-and-privacy/understanding-copyright/). |
| [UK National Archives album](https://www.flickr.com/photos/nationalarchives/albums/72157608619480138/) | Excluded. Its [Flickr reuse policy](https://www.nationalarchives.gov.uk/terms-and-conditions/copyright/) limits reuse to noncommercial purposes. |
| [pingnews album](https://www.flickr.com/photos/pingnews/albums/72157594333669521/) | Discovery only. A third-party upload does not establish the originating item's rights; no qualifying image-specific evidence was admitted. |
| [US National Archives collections](https://www.flickr.com/photos/usnationalarchives/collections/72157620415261553/) | Discovery only. NARA's [reproduction guidance](https://www.archives.gov/research/still-pictures/permissions) and [use policy](https://www.archives.gov/global-pages/privacy.html) require attention to individual rights. US public-domain status does not itself establish worldwide redistribution rights. |
| [pingnews collections](https://www.flickr.com/photos/pingnews/collections/72157600007938726/) | Discovery only. Trace each candidate to authoritative item-specific rights; uploader and collection assertions are insufficient. |
| [Library of Congress collections](https://www.flickr.com/photos/library_of_congress/collections/72157601355524315/) | Discovery only, under the same item-specific requirement as the Library's albums. |

Smithsonian and Getty supplied the included images through their own explicitly designated CC0
media records. Their inclusion does not admit other images from those institutions automatically.

## Quality evidence boundary

The inputs vary handwriting, print, scripts, color and layout. They include numeric tables,
filled forms, calendar columns, maps, musical notation, scientific diagrams and illuminated manuscripts.
This remains a small historical technical corpus. It has no reviewed coverage of contemporary
already-good controls, camera skew, alpha, bleed-through or East and South Asian scripts, among other gaps.
It does not establish representative damage coverage, improved readability or no harm. Independent
rights review, frozen held-out selection and blinded human quality review remain governed by
[document-quality evaluation](document-quality.md); automated inspection is not that human panel.

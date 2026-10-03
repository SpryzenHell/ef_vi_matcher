# Third-party notices and provenance

The project configuration supplied for this project identifies three upstream repositories. Their current revisions were inspected on 2026-10-04 for API/design alignment.

| Component | Repository | Revision inspected | Role |
|---|---|---|---|
| Liquibook | https://github.com/enewhuis/liquibook | 2427613b32f1667abae68a01df6af9ba8270f8e7 | order properties, price-time matching, depth, cancel/replace |
| LightMatchingEngine | https://github.com/gavincyi/LightMatchingEngine | 5e210a809e62a802107831d0ca12498ed32d4717 | compact order/trade API and behavioral reference |
| atomic_queue | https://github.com/max0x7ba/atomic_queue | d6ac7d84bfd541eb91e28a9fc69377f12115a14c | bounded ring-buffer/SPSC design, false-sharing avoidance, huge-page allocator techniques |

The supplied configuration uses the objectcomputing LiquiBook URL; that URL currently resolves to enewhuis/liquibook.

Liquibook source redistribution requires its copyright and licensing notice to be retained. atomic_queue is MIT-licensed. The public LightMatchingEngine tree inspected on 2026-10-04 did not expose a root LICENSE file; verify its terms before redistributing copied source.

The active revamp code does not copy arbitrary upstream source into the new efvi API. The old merged snapshot remains in the repository for provenance, outside the maintained build.

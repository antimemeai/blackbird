# Durable-workflow source acquisitions — 2026-10-14

Study only; no reference implementation executed and no dependency selected. Intact ZIP archives retained; owned importer strips nested Git metadata and filesystem detritus, omits symlinks. Restoration requires absent extraction destination. temporalio/sdk-core now redirects to temporalio/sdk-rust: archive root was read from the ZIP rather than guessed.

| Source | Revision | URL | Intact archive | SHA-256 | Clean extraction |
|---|---|---|---|---|---|
| temporalio/sdk-core | `212e8150066527b57e792b80626fe0d14e7f1da1` | https://codeload.github.com/temporalio/sdk-core/zip/212e8150066527b57e792b80626fe0d14e7f1da1 | `/Users/patrickbeam/projects/quarantine_proj/archives/blackbird-durable-workflows-2026-10-14/temporal-sdk-core-212e81500665.zip` | `3c447c6fb972223c4cf5474b7d2c5a261832401c5ba51e539bb10d2e8d1062c7` | `/Users/patrickbeam/projects/blackbird/quarantine/durable-temporal-sdk-core-212e81500665` |
| dbos-inc/dbos-transact-py | `61426cb95bcd749dd18137b4eb483d02b9975d7d` | https://codeload.github.com/dbos-inc/dbos-transact-py/zip/61426cb95bcd749dd18137b4eb483d02b9975d7d | `/Users/patrickbeam/projects/quarantine_proj/archives/blackbird-durable-workflows-2026-10-14/dbos-python-61426cb95bcd.zip` | `f7450c11a461b1e1a409d99c92d5047534a8be55245d761f1b6ce504d9988f8e` | `/Users/patrickbeam/projects/blackbird/quarantine/durable-dbos-python-61426cb95bcd` |

## Restoration

Restore: `python3 scripts/ingest_zip.py /Users/patrickbeam/projects/quarantine_proj/archives/blackbird-durable-workflows-2026-10-14/temporal-sdk-core-212e81500665.zip /Users/patrickbeam/projects/blackbird/quarantine/durable-temporal-sdk-core-212e81500665 --root sdk-rust-212e8150066527b57e792b80626fe0d14e7f1da1 --skip-symlinks`

Restore: `python3 scripts/ingest_zip.py /Users/patrickbeam/projects/quarantine_proj/archives/blackbird-durable-workflows-2026-10-14/dbos-python-61426cb95bcd.zip /Users/patrickbeam/projects/blackbird/quarantine/durable-dbos-python-61426cb95bcd --root dbos-transact-py-61426cb95bcd749dd18137b4eb483d02b9975d7d --skip-symlinks`

## Acquired literature

Li et al., Distributed Speculative Execution for Resilient Cloud Applications. Source: https://arxiv.org/pdf/2412.13314 ; retained PDF: `papers/workflows-2026-10-14/distributed-speculative-execution-2412.13314.pdf`; SHA-256 `c272a789b831de2b03f7cba7c2eecf3e80d52f531a53478c58670a123bcc8185`. Restore: `curl -fL https://arxiv.org/pdf/2412.13314 -o papers/workflows-2026-10-14/distributed-speculative-execution-2412.13314.pdf`. This URL resolves the latest paper version; the digest identifies the exact bytes read here and must be checked on restoration. Text extracted to ignored `context/durable-dse-2412.13314.txt`; no reference implementation executed.

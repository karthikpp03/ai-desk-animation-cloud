# Worker

Cloudflare Worker API for the standalone animation system.

The Worker is intentionally stateless apart from JSON/files stored in the configured GitHub repository. It bridges the website, GitHub Contents API, OpenAI fallback, and ESP32.

No R2 bucket, Firebase, database, VPS, or other storage service is required.

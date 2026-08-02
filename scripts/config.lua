return {
    -- Delay after a server-side event so replicated parameter/container state settles.
    event_delay_ms = 100,

    -- Enable extra diagnostic lines while validating a new Palworld build.
    debug_logging = false,

    -- Keep this false in normal operation; it logs only hook/processing summaries.
    log_skipped = false,
}

"""Ground station for sun-tracker."""


def main() -> None:
    from ground.cli import main as cli_main

    raise SystemExit(cli_main())

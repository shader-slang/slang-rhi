<!--
SPDX-FileCopyrightText: The Khronos Group, Inc.
SPDX-License-Identifier: CC-BY-4.0
-->

## Contributing

This project welcomes contributions and suggestions. Contributions require you to agree to a Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant the rights to use your contribution.

When you submit a pull request, a CLA bot will determine whether you need to sign a CLA. Simply follow the instructions provided.

## Development and documentation

See [Building and testing](docs/building.md) to set up a development build and
run tests. Public API changes follow the
[compatibility policy](README.md#project-status-and-compatibility); preserving
source or binary compatibility with earlier versions is not a requirement.
Describe API changes and any required caller updates in the pull request, and
update affected backends, debug-layer wrappers, examples, and tests together.

Keep documentation close to the code it describes:

- Use the README for the project overview, compatibility policy, quick start,
  and links to more detail.
- Document API behavior, errors, and ownership in public header comments.
- Keep build instructions and focused usage or design guides in `docs/`,
  and link new guides from the README.
- Update [API implementation status](docs/api.md) when backend support changes.
- Use runnable examples in `examples/` for complete workflows.

Update the relevant documentation in the same pull request as the behavior it
describes. Link to existing guidance instead of duplicating it.

## AI-Assisted Contributions

By submitting a Contribution to this repository, you additionally represent that, to the extent any of Your Contributions were developed with the assistance of artificial intelligence tools or AI-generated code, You have exercised sufficient review, judgment, and creative direction over such tools and resulting material to reasonably consider it Your original creation, and You are not aware of any third-party license, intellectual property claim, or other restriction arising from such use that is associated with any part of Your Contribution or use thereof.

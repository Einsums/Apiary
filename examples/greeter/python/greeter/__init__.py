# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""greeter - the Apiary example extension, a package around its compiled core.

Everything here is bound from C++ by Apiary into ``greeter._core``; the
package re-exports it, the layout Apiary's generated stubs describe.
"""

from ._core import *  # noqa: F401,F403

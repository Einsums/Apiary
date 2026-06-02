#  Copyright (c) The Einsums Developers. All rights reserved.
#  Licensed under the MIT License. See LICENSE.txt in the project root.
project = "Apiary"
copyright = "The Einsums Developers"
author = "The Einsums Developers"

# cpp/c domains are built in; no extra extensions needed for the dogfood site.
extensions = []
primary_domain = "cpp"
default_role = "literal"
master_doc = "index"
exclude_patterns = ["_build", "api/*.json"]

html_theme = "alabaster"
html_title = "Apiary"

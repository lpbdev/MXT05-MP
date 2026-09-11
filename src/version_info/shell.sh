if command -v svn >/dev/null 2>&1 && svn info >/dev/null 2>&1; then \
  svn info 2>/dev/null | grep '^Revision:' | sed 's/^Revision:[[:space:]]*//'; \
else echo unknown; fi

s = File.read("index.html")
s.gsub!(/\n/, "\\n");
s.gsub!(/\"/, '\"');
#s = "#define _INDEX_HTML \"#{s}\""
#s = "static const char* _INDEX_HTML = \"#{s}\";"
s = "const char index_html[] PROGMEM = \"#{s}\";"
File.write("../include/index_html.h", s)

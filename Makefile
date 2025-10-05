CXX       = g++
CXXFLAGS  = -std=c++17 -Wall -Wextra
OUTDIR    = bin

all: $(OUTDIR)/server $(OUTDIR)/client

# 生成目录
$(OUTDIR):
	mkdir -p $(OUTDIR)

# 生成 server
$(OUTDIR)/server: server.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) server.cpp hashtable.cpp z_set.cpp -o $@

# 生成 client
$(OUTDIR)/client: client.cpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) client.cpp -o $@

clean:
	rm -rf $(OUTDIR)

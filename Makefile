xldd: xldd.cpp
	$(CXX) $(CXXFLAGS) -std=gnu++20 -o $@ $< -lelf

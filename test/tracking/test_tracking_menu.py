"""Verify the real settings accordion can expose the new ninth category."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'examples/companion_radio/ui-new/AccordionList.h').read_text().replace('#pragma once','').replace('#include "icons.h"','')
harness=r'''
#include <cassert>
#include <cstdint>
const char KEY_UP=1,KEY_DOWN=2,KEY_ENTER=3;
struct DisplayDriver {};
template<class F> void drawList(DisplayDriver&,int,int&,int&,F) {}
SOURCE
int main() {
 uint8_t counts[9]={1,1,1,1,1,1,1,1,1};
 AccordionList list;list.begin(counts,9);
 assert(list.visibleCount()==9 && list.collapsed(8));
 for(int i=0;i<8;++i)list.handleInput(KEY_DOWN);
 assert(list.selected().sec==8);
 assert(list.handleInput(KEY_ENTER)==AccordionList::HANDLED);
 assert(!list.collapsed(8) && list.collapsed(0) && list.visibleCount()==10);
 list.handleInput(KEY_DOWN);
 assert(list.selected().sec==8 && list.selected().item==0);
 assert(list.handleInput(KEY_ENTER)==AccordionList::ACTIVATED);
 list.toggle(8);assert(list.visibleCount()==9 && list.collapsed(8));
}
'''.replace('SOURCE',source)
with tempfile.TemporaryDirectory() as tmp:
 cpp,binary=Path(tmp)/'menu.cpp',Path(tmp)/'menu'
 cpp.write_text(harness)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('Ninth settings category, collapse state and item activation passed')

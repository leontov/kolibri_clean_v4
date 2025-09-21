import { NavLink, Route, Routes } from "react-router-dom";
import Dashboard from "./routes/Dashboard";
import Ledger from "./routes/Ledger";
import Skills from "./routes/Skills";
import Settings from "./routes/Settings";
import XAI from "./routes/XAI";
import SseStatus from "./components/SseStatus";

const navLinkClass = ({ isActive }: { isActive: boolean }) =>
  `px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
    isActive ? "bg-brand-500/30 text-white" : "text-slate-200 hover:bg-slate-800/60"
  }`;

function App() {
  return (
    <div className="min-h-screen flex flex-col">
      <header className="border-b border-slate-800/70 bg-slate-950/80 backdrop-blur">
        <div className="mx-auto flex w-full max-w-6xl items-center justify-between px-6 py-4">
          <div className="flex items-center gap-4">
            <div className="h-10 w-10 rounded-xl bg-brand-500/40" />
            <div>
              <h1 className="text-xl font-semibold">Kolibri Observatory</h1>
              <p className="text-xs text-slate-400">FA-10 | KPRL chain monitor</p>
            </div>
          </div>
          <SseStatus />
        </div>
        <nav className="mx-auto flex w-full max-w-6xl gap-2 px-6 pb-4">
          <NavLink to="/" className={navLinkClass} end>
            Dashboard
          </NavLink>
          <NavLink to="/ledger" className={navLinkClass}>
            Ledger
          </NavLink>
          <NavLink to="/xai" className={navLinkClass}>
            XAI
          </NavLink>
          <NavLink to="/skills" className={navLinkClass}>
            Skills
          </NavLink>
          <NavLink to="/settings" className={navLinkClass}>
            Settings
          </NavLink>
        </nav>
      </header>
      <main className="mx-auto flex w-full max-w-6xl flex-1 flex-col gap-6 px-6 py-6">
        <Routes>
          <Route path="/" element={<Dashboard />} />
          <Route path="/ledger" element={<Ledger />} />
          <Route path="/skills" element={<Skills />} />
          <Route path="/settings" element={<Settings />} />
          <Route path="/xai" element={<XAI />} />
          <Route path="/offline" element={<div className="card">Offline cache active.</div>} />
        </Routes>
      </main>
    </div>
  );
}

export default App;

export default function Skills() {
  return (
    <section className="space-y-4">
      <h2 className="text-xl font-semibold">SkillStore snapshot</h2>
      <div className="card">
        <p className="text-sm text-slate-300">
          Kolibri ships with a placeholder SkillStore. Integrate REST APIs to surface curated skills with compliance badges.
        </p>
        <ul className="mt-4 list-disc space-y-2 pl-5 text-sm text-slate-400">
          <li>Demo Skill — deterministic FA-10 explorer</li>
          <li>Forecast Skill — streaming metric deltas</li>
          <li>Compliance Skill — privacy guardrails</li>
        </ul>
      </div>
    </section>
  );
}
